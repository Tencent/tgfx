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

#include "gpu/DefaultTexture.h"

namespace tgfx {
class ExtendedTexture : public DefaultTexture {
 public:
  ExtendedTexture(std::unique_ptr<TextureSampler> sampler, int width, int height, int extendedWidth,
                  int extendedHeight, ImageOrigin origin = ImageOrigin::TopLeft);

  size_t memoryUsage() const override;

  Point getTextureCoord(float x, float y) const override {
    return {x / static_cast<float>(extendedWidth), y / static_cast<float>(extendedHeight)};
  }

  BackendTexture getBackendTexture() const override {
    return getSampler()->getBackendTexture(extendedWidth, extendedHeight);
  }

 private:
  int extendedWidth = 0;
  int extendedHeight = 0;
};
}  // namespace tgfx
