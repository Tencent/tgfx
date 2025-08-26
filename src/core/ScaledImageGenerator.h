/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "tgfx/core/ImageCodec.h"

namespace tgfx {

class ScaledImageGenerator : public ImageGenerator {
 public:
  static float GetCodecScale(float drawScale);

  static std::shared_ptr<ScaledImageGenerator> MakeFrom(const std::shared_ptr<ImageCodec>& codec,
                                                        int width, int height);

  ~ScaledImageGenerator() override = default;

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool asyncSupport() const override {
    return source->asyncSupport();
  }

  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override;

 private:
  std::shared_ptr<ImageCodec> source = nullptr;

  explicit ScaledImageGenerator(int width, int height, const std::shared_ptr<ImageCodec>& codec);
};
}  // namespace tgfx
