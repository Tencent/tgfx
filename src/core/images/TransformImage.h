/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <optional>
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * The base class for all images that have a single source image and apply various transformations.
 */
class TransformImage : public Image {
 public:
  explicit TransformImage(std::shared_ptr<Image> source);

  bool hasMipmaps() const override {
    return source->hasMipmaps();
  }

  bool isFullyDecoded() const override {
    return source->isFullyDecoded();
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return source->colorSpace();
  }

  std::shared_ptr<Image> source = nullptr;

 protected:
  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  std::shared_ptr<TextureProxy> lockTextureProxySubset(
      const TPArgs& args, const Rect& drawRect, const SamplingOptions& samplingOptions = {}) const;

  virtual std::optional<Matrix> concatUVMatrix(const Matrix* uvMatrix) const = 0;

  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  virtual std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const = 0;
};
}  // namespace tgfx
