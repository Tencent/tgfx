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
    BlockBuffer* buffer, std::shared_ptr<TextureProxy> textureProxy, const Matrix& uvMatrix,
    std::shared_ptr<ColorSpace> dstColorSpace) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return buffer->make<GLSLDeviceSpaceTextureEffect>(std::move(textureProxy), uvMatrix,
                                                    std::move(dstColorSpace));
}

GLSLDeviceSpaceTextureEffect::GLSLDeviceSpaceTextureEffect(
    std::shared_ptr<TextureProxy> textureProxy, const Matrix& uvMatrix,
    std::shared_ptr<ColorSpace> colorSpace)
    : DeviceSpaceTextureEffect(std::move(textureProxy), uvMatrix, std::move(colorSpace)) {
}

void GLSLDeviceSpaceTextureEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  auto deviceCoordMatrixName = uniformHandler->addUniform(
      "DeviceCoordMatrix", UniformFormat::Float3x3, ShaderStage::Fragment);
  fragBuilder->codeAppendf("vec3 deviceCoord = %s * vec3(gl_FragCoord.xy, 1.0);",
                           deviceCoordMatrixName.c_str());
  std::string coordName = "deviceCoord.xy";
  fragBuilder->codeAppendf("%s = ", args.outputColor.c_str());
  fragBuilder->appendTextureLookup((*args.textureSamplers)[0], coordName);
  fragBuilder->codeAppend(";");
  auto srcColorSpace = textureProxy->getTextureView()->gamutColorSpace();
  auto steps = std::make_shared<ColorSpaceXformSteps>(
      srcColorSpace.get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
  fragBuilder->appendColorGamutXform(args.outputColor.c_str(), steps.get());
  if (textureProxy->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = %s.a * %s;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  } else {
    fragBuilder->codeAppendf("%s = %s * %s.a;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  }
}

void GLSLDeviceSpaceTextureEffect::onSetData(UniformData* /*vertexUniformData*/,
                                             UniformData* fragmentUniformData) const {
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return;
  }
  auto srcColorSpace = textureProxy->getTextureView()->gamutColorSpace();
  auto steps = std::make_shared<ColorSpaceXformSteps>(
      srcColorSpace.get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
  ColorSpaceXformHelper helper{};
  helper.setData(fragmentUniformData, steps.get());
  auto deviceCoordMatrix = uvMatrix;
  auto scale = textureView->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  fragmentUniformData->setData("DeviceCoordMatrix", deviceCoordMatrix);
}
}  // namespace tgfx
