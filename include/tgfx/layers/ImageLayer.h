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
   * Returns true if the image is smoothed when scaled.
   */
  bool smoothing() const {
    return _smoothing;
  }

  /**
   * Sets whether the image is smoothed when scaled.
   */
  void setSmoothing(bool value);

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
  ImageLayer() = default;

 private:
  bool _smoothing = true;
  std::shared_ptr<Image> _image;
};
}  // namespace tgfx
