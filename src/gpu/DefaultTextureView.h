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

#include "gpu/TextureView.h"

namespace tgfx {
/**
 * DefaultTextureView is a simple TextureView implementation that stores pixel data using a single
 * GPUTexture.
 */
class DefaultTextureView : public TextureView {
 public:
  explicit DefaultTextureView(std::unique_ptr<GPUTexture> texture,
                              ImageOrigin origin = ImageOrigin::TopLeft);

  size_t memoryUsage() const override;

  GPUTexture* getTexture() const override {
    return _texture.get();
  }

 protected:
  std::unique_ptr<GPUTexture> _texture = nullptr;

  void onReleaseGPU() override {
    _texture->release(context->gpu());
  }
};
}  // namespace tgfx
