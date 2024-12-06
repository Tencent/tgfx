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

#include "core/images/OffscreenImage.h"

namespace tgfx {
/**
 * ScaleImage is an image that scales another image.
 */
class ScaleImage : public OffscreenImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, float scale,
                                         const SamplingOptions& sampling);

  int width() const override;

  int height() const override;

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool isFullyDecoded() const override {
    return source->isFullyDecoded();
  }

  std::shared_ptr<Image> makeScaled(float scale, const SamplingOptions& sampling) const override;

 protected:
  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

  bool onDraw(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags) const override;

 private:
  std::shared_ptr<Image> source = nullptr;
  float scale = 1.0f;
  SamplingOptions sampling = {};

  ScaleImage(UniqueKey uniqueKey, std::shared_ptr<Image> source, float scale,
             const SamplingOptions& sampling);
};
}  // namespace tgfx
