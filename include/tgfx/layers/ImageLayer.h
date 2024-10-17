/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "tgfx/core/Image.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
/**
 * ImageLayer represents a layer that displays an image.
 */
class ImageLayer : public Layer {
 public:
  /**
   * Creates a new image layer.
   */
  static std::shared_ptr<ImageLayer> Make();

  LayerType type() const override {
    return LayerType::Image;
  }

  /**
   * Returns the sampling options used to draw the image. The default value is
   * SamplingOptions(FilterMode::Linear, MipmapMode::Linear).
   */
  SamplingOptions sampling() const {
    return _sampling;
  }

  /**
   * Sets the sampling options used to display the image.
   */
  void setSampling(const SamplingOptions& value);

  /**
   * Returns the image displayed by this layer.
   */
  std::shared_ptr<Image> image() const {
    return _image;
  }

  /**
   * Sets the image displayed by this layer.
   */
  void setImage(std::shared_ptr<Image> value);

 protected:
  ImageLayer() : _sampling(FilterMode::Linear, MipmapMode::Linear) {
  }

  void onDraw(Canvas* canvas, float alpha) override;

  void measureContentBounds(Rect* rect) const override;

 private:
  SamplingOptions _sampling;
  std::shared_ptr<Image> _image = nullptr;
};
}  // namespace tgfx
