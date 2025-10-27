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

#include "DeviceSpaceTextureEffect.h"

namespace tgfx {
DeviceSpaceTextureEffect::DeviceSpaceTextureEffect(std::shared_ptr<TextureProxy> textureProxy,
                                                   const Matrix& uvMatrix)
    : FragmentProcessor(ClassID()), textureProxy(std::move(textureProxy)), uvMatrix(uvMatrix) {
}

std::shared_ptr<Texture> DeviceSpaceTextureEffect::onTextureAt(size_t) const {
  auto textureView = textureProxy->getTextureView();
  return textureView == nullptr ? nullptr : textureView->getTexture();
}
}  // namespace tgfx
