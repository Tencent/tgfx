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

#include "gpu/resources/TextureView.h"

namespace tgfx {
/**
 * DefaultTextureView is a simple TextureView implementation that stores pixel data using a single
 * Texture.
 */
class DefaultTextureView : public TextureView {
 public:
  explicit DefaultTextureView(std::shared_ptr<Texture> texture,
                              ImageOrigin origin = ImageOrigin::TopLeft);

  size_t memoryUsage() const override;

  std::shared_ptr<Texture> getTexture() const override {
    return _texture;
  }

 protected:
  std::shared_ptr<Texture> _texture = nullptr;
};
}  // namespace tgfx
