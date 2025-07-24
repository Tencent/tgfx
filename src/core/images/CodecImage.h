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
#include "core/images/GeneratorImage.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageCodec.h"
namespace tgfx {

class CodecImage : public GeneratorImage {
 public:
  CodecImage(int width, int height, UniqueKey uniqueKey, std::shared_ptr<ImageCodec> codec);

  std::shared_ptr<ImageCodec> getCodec() const;

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  std::shared_ptr<Image> makeScaled(int newWidth, int newHeight, const SamplingOptions& sampling) const override;

 protected:
  Type type() const override {
    return Type::Codec;
  }

  std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args, const UniqueKey& key) const override;

 private:
  int _width;
  int _height;
};

}  // namespace tgfx