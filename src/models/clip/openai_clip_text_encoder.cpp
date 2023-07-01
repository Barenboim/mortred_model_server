/************************************************
 * Copyright MaybeShewill-CV. All Rights Reserved.
 * Author: MaybeShewill-CV
 * File: OpenAiClipTextEncoder.cpp
 * Date: 23-6-26
 ************************************************/

#include "openai_clip_text_encoder.h"

#include <chrono>

#include "glog/logging.h"
#include "MNN/Interpreter.hpp"

#include "common/file_path_util.h"
#include "common/cv_utils.h"
#include "common/time_stamp.h"

namespace jinq {
namespace models {

using jinq::common::CvUtils;
using jinq::common::StatusCode;
using jinq::common::FilePathUtil;
using jinq::common::Timestamp;

namespace clip {

class OpenAiClipTextEncoder::Impl {
  public:
    /***
     *
     */
    Impl() = default;

    /***
     *
     */
    ~Impl() = default;

    /***
     *
     * @param cfg
     * @return
     */
    StatusCode init(const decltype(toml::parse(""))& cfg);

    /***
     *
     * @param input_image
     * @param text_embeddings
     * @return
     */
    StatusCode encode(const cv::Mat& input_image, std::vector<float>& text_embeddings);

    /***
     *
     * @return
     */
    std::vector<int> get_encoder_input_shape() const {
        return _m_input_shape;
    }

    /***
     * if model successfully initialized
     * @return
     */
    bool is_successfully_initialized() const {
        return _m_successfully_init_model;
    }

  private:
    // model file path
    std::string _m_model_path;

    // model compute thread nums
    uint16_t _m_thread_nums = 1;

    // model backend device
    std::string _m_model_device;

    // model input/output names
    std::string _m_input_name;
    std::string _m_output_name;

    // model session
    std::unique_ptr<MNN::Interpreter> _m_net;
    MNN::Session* _m_session = nullptr;
    MNN::Tensor* _m_input_tensor = nullptr;
    MNN::Tensor* _m_output_tensor = nullptr;

    // model input/output shape info
    std::vector<int> _m_input_shape;
    std::vector<int> _m_output_shape;

    // init flag
    bool _m_successfully_init_model = false;

  private:
    /***
     *
     * @param input_image
     * @return
     */
    cv::Mat preprocess_image(const cv::Mat& input_image);
};

/************ Impl Implementation ************/

/***
 *
 * @param cfg
 * @return
 */
StatusCode OpenAiClipTextEncoder::Impl::init(const decltype(toml::parse("")) &cfg) {
    // init sam encoder configs
    auto cfg_content = cfg.at("OPENAI_CLIP_VIT_ENCODER");
    _m_model_path = cfg_content["model_file_path"].as_string();
    if (!FilePathUtil::is_file_exist(_m_model_path)) {
        LOG(ERROR) << "openai clip vit encoder model file path: " << _m_model_path << " not exists";
        _m_successfully_init_model = false;
        return StatusCode::MODEL_INIT_FAILED;
    }
    _m_net = std::unique_ptr<MNN::Interpreter>(MNN::Interpreter::createFromFile(_m_model_path.c_str()));
    _m_thread_nums = cfg_content["model_threads_num"].as_integer();
    _m_model_device = cfg_content["compute_backend"].as_string();
    MNN::ScheduleConfig mnn_config;
    mnn_config.numThread = _m_thread_nums;
    mnn_config.type = MNN_FORWARD_CPU;
    if (std::strcmp(_m_model_device.c_str(), "cuda") == 0) {
        mnn_config.type = MNN_FORWARD_CUDA;
    }
    MNN::BackendConfig backend_config;
    if (!cfg_content.contains("backend_precision_mode")) {
        LOG(WARNING) << "Config doesn\'t have backend_precision_mode field default Precision_Normal";
        backend_config.precision = MNN::BackendConfig::Precision_Normal;
    } else {
        backend_config.precision = static_cast<MNN::BackendConfig::PrecisionMode>(cfg_content.at("backend_precision_mode").as_integer());
    }
    if (!cfg_content.contains("backend_power_mode")) {
        LOG(WARNING) << "Config doesn\'t have backend_power_mode field default Power_Normal";
        backend_config.power = MNN::BackendConfig::Power_Normal;
    } else {
        backend_config.power = static_cast<MNN::BackendConfig::PowerMode>(cfg_content.at("backend_power_mode").as_integer());
    }
    mnn_config.backendConfig = &backend_config;

    _m_session = _m_net->createSession(mnn_config);

    _m_input_name = "pixel_values";
    _m_output_name = "output";

    _m_input_tensor = _m_net->getSessionInput(_m_session, _m_input_name.c_str());
    _m_output_tensor = _m_net->getSessionOutput(_m_session, _m_output_name.c_str());

    _m_input_shape = _m_input_tensor->shape();
    _m_output_shape = _m_output_tensor->shape();
    if (_m_input_shape.size() != 4 || _m_output_shape.size() != 4) {
        LOG(ERROR) << "invalid encoder input/output node shape";
        return StatusCode::MODEL_INIT_FAILED;
    }

    _m_successfully_init_model = true;
    LOG(INFO) << "Successfully load openai clip vit encoder";
    return StatusCode::OJBK;
}

/***
 *
 * @param input_image
 * @param text_embeddings
 * @return
 */
StatusCode OpenAiClipTextEncoder::Impl::encode(
    const cv::Mat &input_image,
    std::vector<float> &text_embeddings) {
    // preprocess image
    auto preprocessed_image = preprocess_image(input_image);
    auto input_tensor_values = CvUtils::convert_to_chw_vec(preprocessed_image);
    if (input_tensor_values.empty()) {
        LOG(ERROR) << "empty input data for sam vit encoder";
        return StatusCode::MODEL_EMPTY_INPUT_IMAGE;
    }

    // run encoder
    auto input_tensor_user = MNN::Tensor(_m_input_tensor, MNN::Tensor::DimensionType::CAFFE);
    auto input_tensor_data = input_tensor_user.host<float>();
    auto input_tensor_size = input_tensor_user.size();
    ::memcpy(input_tensor_data, input_tensor_values.data(), input_tensor_size);
    _m_input_tensor->copyFromHostTensor(&input_tensor_user);

    _m_net->runSession(_m_session);

    MNN::Tensor output_tensor_user(_m_output_tensor, MNN::Tensor::DimensionType::CAFFE);
    _m_output_tensor->copyToHostTensor(&output_tensor_user);

    auto embeds_size = std::accumulate(
        std::begin(_m_output_shape), std::end(_m_output_shape), 1, std::multiplies());
    text_embeddings.resize(embeds_size);
    auto img_embeds_val = output_tensor_user.host<float>();
    for (auto idx = 0; idx < embeds_size; ++idx) {
        text_embeddings[idx] = img_embeds_val[idx];
    }

    return StatusCode::OJBK;
}

/***
 *
 * @param input_image
 * @return
 */
cv::Mat OpenAiClipTextEncoder::Impl::preprocess_image(const cv::Mat &input_image) {

    auto input_node_h = static_cast<int>(_m_input_shape[2]);
    auto input_node_w = static_cast<int>(_m_input_shape[3]);
    auto ori_img_width = static_cast<float>(input_image.size().width);
    auto ori_img_height = static_cast<float>(input_image.size().height);
    auto long_side = std::max(input_image.size().height, input_image.size().width);
    float scale = static_cast<float>(input_node_h) / static_cast<float>(long_side);
    cv::Size target_size = cv::Size(
        static_cast<int>(scale * ori_img_width), static_cast<int>(scale * ori_img_height));

    cv::Mat result;
    cv::cvtColor(input_image, result, cv::COLOR_BGR2RGB);
    cv::resize(input_image, result,target_size);
    result.convertTo(result, CV_32FC3);

    cv::subtract(result, cv::Scalar(123.675, 116.28, 103.53), result);
    cv::divide(result, cv::Scalar(58.395, 57.12, 57.375), result);

    // pad image
    auto pad_h = input_node_h - target_size.height;
    auto pad_w = input_node_w - target_size.width;
    cv::copyMakeBorder(result, result, 0, pad_h, 0, pad_w, cv::BORDER_CONSTANT, 0.0);

    return result;
}

/***
 *
 */
OpenAiClipTextEncoder::OpenAiClipTextEncoder() {
    _m_pimpl = std::make_unique<Impl>();
}

/***
 *
 */
OpenAiClipTextEncoder::~OpenAiClipTextEncoder() = default;

/***
 *
 * @param cfg
 * @return
 */
StatusCode OpenAiClipTextEncoder::init(const decltype(toml::parse("")) &cfg) {
    return _m_pimpl->init(cfg);
}

/***
 *
 * @param input_image
 * @param text_embeddings
 * @return
 */
StatusCode OpenAiClipTextEncoder::encode(const cv::Mat &input_image, std::vector<float> &text_embeddings) {
    return _m_pimpl->encode(input_image, text_embeddings);
}

/***
 *
 * @return
 */
std::vector<int> OpenAiClipTextEncoder::get_encoder_input_shape() const {
    return _m_pimpl->get_encoder_input_shape();
}

/***
 *
 * @return
 */
bool OpenAiClipTextEncoder::is_successfully_initialized() const {
    return _m_pimpl->is_successfully_initialized();
}

}
}
}