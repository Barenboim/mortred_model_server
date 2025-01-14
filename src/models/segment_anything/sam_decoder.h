/************************************************
 * Copyright MaybeShewill-CV. All Rights Reserved.
 * Author: MaybeShewill-CV
 * File: SamDecoder.h
 * Date: 23-6-7
 ************************************************/

#ifndef MORTRED_MODEL_SERVER_SAMDECODER_H
#define MORTRED_MODEL_SERVER_SAMDECODER_H

#include <vector>

#include <opencv2/opencv.hpp>
#include "toml/toml.hpp"

#include "common/status_code.h"

namespace jinq {
namespace models {
namespace segment_anything {

/***
 *
 */
class SamDecoder {
  public:
    /***
    * constructor
    * @param config
     */
    SamDecoder();

    /***
     *
     */
    ~SamDecoder();

    /***
    * constructor
    * @param transformer
     */
    SamDecoder(const SamDecoder& transformer) = delete;

    /***
     * constructor
     * @param transformer
     * @return
     */
    SamDecoder& operator=(const SamDecoder& transformer) = delete;

    /***
     *
     * @param toml
     * @return
     */
    jinq::common::StatusCode init(const decltype(toml::parse(""))& cfg);

    /***
     *
     * @param ori_img_size
     */
    void set_ori_image_size(const cv::Size& ori_img_size);

    /***
     *
     * @param image_embeddings
     * @param bboxes
     * @param predicted_masks
     * @return
     */
    jinq::common::StatusCode decode(
        const std::vector<float>& image_embeddings,
        const std::vector<cv::Rect2f>& bboxes,
        std::vector<cv::Mat>& predicted_masks);


    /***
     * if model successfully initialized
     * @return
     */
    bool is_successfully_initialized() const;

  private:
    class Impl;
    std::unique_ptr<Impl> _m_pimpl;
};
}
}
}

#endif // MORTRED_MODEL_SERVER_SAMDECODER_H
