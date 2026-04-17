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

#include "GLSLTextureGradientColorizer.h"

namespace tgfx {
PlacementPtr<TextureGradientColorizer> TextureGradientColorizer::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> gradient) {
  if (gradient == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLTextureGradientColorizer>(std::move(gradient));
}

GLSLTextureGradientColorizer::GLSLTextureGradientColorizer(std::shared_ptr<TextureProxy> gradient)
    : TextureGradientColorizer(std::move(gradient)) {
}
}  // namespace tgfx
