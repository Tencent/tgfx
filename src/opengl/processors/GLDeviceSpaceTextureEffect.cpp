/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLDeviceSpaceTextureEffect.h"

namespace tgfx {
std::unique_ptr<DeviceSpaceTextureEffect> DeviceSpaceTextureEffect::Make(
    std::shared_ptr<TextureProxy> textureProxy, ImageOrigin deviceOrigin) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<DeviceSpaceTextureEffect>(
      new GLDeviceSpaceTextureEffect(std::move(textureProxy), deviceOrigin));
}

GLDeviceSpaceTextureEffect::GLDeviceSpaceTextureEffect(std::shared_ptr<TextureProxy> textureProxy,
                                                       ImageOrigin deviceOrigin)
    : DeviceSpaceTextureEffect(std::move(textureProxy), deviceOrigin) {
}

void GLDeviceSpaceTextureEffect::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto* uniformHandler = args.uniformHandler;
  auto deviceCoordMatrixName =
      uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float3x3, "DeviceCoordMatrix");
  auto scaleName = uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "CoordScale");
  fragBuilder->codeAppendf("vec3 deviceCoord = %s * vec3(gl_FragCoord.xy * %s, 1.0);",
                           deviceCoordMatrixName.c_str(), scaleName.c_str());
  std::string coordName = "deviceCoord.xy";
  fragBuilder->codeAppendf("%s = ", args.outputColor.c_str());
  fragBuilder->appendTextureLookup((*args.textureSamplers)[0], coordName);
  fragBuilder->codeAppend(";");
}

void GLDeviceSpaceTextureEffect::onSetData(UniformBuffer* uniformBuffer) const {
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    return;
  }
  float scales[] = {1.f / static_cast<float>(texture->width()),
                    1.f / static_cast<float>(texture->height())};
  uniformBuffer->setData("CoordScale", scales);
  Matrix deviceCoordMatrix = Matrix::I();
  if (deviceOrigin == ImageOrigin::BottomLeft) {
    deviceCoordMatrix.postScale(1, -1);
    deviceCoordMatrix.postTranslate(0, 1);
  }
  auto scale = texture->getTextureCoord(static_cast<float>(texture->width()),
                                        static_cast<float>(texture->height()));
  deviceCoordMatrix.postScale(scale.x, scale.y);
  uniformBuffer->setData("DeviceCoordMatrix", deviceCoordMatrix);
}
}  // namespace tgfx
