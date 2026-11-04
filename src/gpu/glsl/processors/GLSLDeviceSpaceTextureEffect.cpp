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

#include "GLSLDeviceSpaceTextureEffect.h"

namespace tgfx {
PlacementPtr<DeviceSpaceTextureEffect> DeviceSpaceTextureEffect::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> textureProxy, const Matrix& uvMatrix) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLDeviceSpaceTextureEffect>(std::move(textureProxy), uvMatrix);
}

GLSLDeviceSpaceTextureEffect::GLSLDeviceSpaceTextureEffect(
    std::shared_ptr<TextureProxy> textureProxy, const Matrix& uvMatrix)
    : DeviceSpaceTextureEffect(std::move(textureProxy), uvMatrix) {
}

void GLSLDeviceSpaceTextureEffect::onSetData(UniformData* /*vertexUniformData*/,
                                             UniformData* fragmentUniformData) const {
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return;
  }
  auto deviceCoordMatrix = uvMatrix;
  auto scale = textureView->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  fragmentUniformData->setData("DeviceCoordMatrix", deviceCoordMatrix);
}
}  // namespace tgfx
