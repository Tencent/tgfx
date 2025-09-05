/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include <memory>
#include "core/images/BufferImage.h"
#include "core/images/GeneratorImage.h"
#include "core/utils/NextCacheScaleLevel.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageCodec.h"

namespace tgfx {

class CodecImage : public GeneratorImage {
 public:
  CodecImage(std::shared_ptr<ImageCodec> codec, int width, int height, bool mipmapped);

  std::shared_ptr<ImageCodec> getCodec() const;

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

 protected:
  Type type() const override {
    return Type::Codec;
  }

  float getRasterizedScale(float drawScale) const override;

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

 private:
  int _width;
  int _height;

  friend class BufferImage;
};

}  // namespace tgfx
