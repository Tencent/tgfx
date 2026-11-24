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
#include "gpu/glsl/GLSLOperatorEmitter.h"

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

void GLSLDeviceSpaceTextureEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  auto deviceCoordMatrixName = uniformHandler->addUniform(
      "DeviceCoordMatrix", UniformFormat::Float3x3, ShaderStage::Fragment);
  const auto& sampler = (*args.textureSamplers)[0];
  const int swizzleCode =
      ShaderBuilder::ReadSwizzleCode(uniformHandler->getSamplerSwizzle(sampler));
  auto coordEntry = fragBuilder->appendFunction("tgfxDeviceCoord");
  auto readEntry = fragBuilder->appendFunction("tgfxTextureSample");
  auto mulEntry = fragBuilder->appendFunction(textureProxy->isAlphaOnly() ? "tgfxAlphaModulate"
                                                                          : "tgfxMulAlpha");
  if (!coordEntry.empty() && !readEntry.empty() && !mulEntry.empty() && swizzleCode >= 0) {
    if (args.irBuilder == nullptr || args.inputValue == INVALID_VALUE_ID ||
        args.outputValue == INVALID_VALUE_ID) {
      return;
    }
    const auto samplerValue = args.samplerValue(sampler);
    if (samplerValue == INVALID_VALUE_ID) {
      args.irBuilder->MarkFailed("the device-space sampler has no IR value");
      return;
    }
    const auto matrixValue = args.irBuilder->InternValue(deviceCoordMatrixName, SLType::Float3x3,
                                                         Precision::High, IRValueKind::Uniform);
    const auto fragCoordValue = args.irBuilder->CreateBuiltin("gl_FragCoord", SLType::Float4);
    const auto xyValue = args.irBuilder->CreateSwizzle(fragCoordValue, SLType::Float2, "xy");
    const auto deviceCoordValue = args.irBuilder->CreateCall(
        OperatorID::DeviceCoord, SLType::Float2, {matrixValue, xyValue}, "deviceCoord");
    const auto deviceReadValue =
        args.irBuilder->CreateTextureSample(samplerValue, deviceCoordValue, SLType::Float4,
                                            "deviceRead", SLType::Texture2DSampler, swizzleCode);
    const auto op = textureProxy->isAlphaOnly() ? OperatorID::AlphaModulate : OperatorID::MulAlpha;
    const auto result =
        args.irBuilder->CreateCall(static_cast<OperatorID>(op), SLType::Float4,
                                   {deviceReadValue, args.inputValue}, args.outputColor);
    args.irBuilder->StoreOutput(args.outputValue, result);

    std::string coordExpression = {};
    std::string readExpression = {};
    std::string mulExpression = {};
    std::string error = {};
    if (!GLSLOperatorEmitter::EmitValue(args.irBuilder->IR(), deviceCoordValue, &coordExpression,
                                        &error) ||
        !GLSLOperatorEmitter::EmitValue(args.irBuilder->IR(), deviceReadValue, &readExpression,
                                        &error) ||
        !GLSLOperatorEmitter::EmitValue(args.irBuilder->IR(), result, &mulExpression, &error)) {
      args.irBuilder->MarkFailed(error);
      return;
    }
    const auto samplerName = uniformHandler->getSamplerVariable(sampler).name();
    fragBuilder->codeAppend("vec2 deviceCoord;");
    fragBuilder->appendCall("deviceCoord", coordEntry, {deviceCoordMatrixName, "gl_FragCoord.xy"},
                            "", coordExpression);
    fragBuilder->codeAppend("vec4 deviceRead;");
    fragBuilder->appendCall(
        "deviceRead", readEntry,
        {samplerName, "deviceCoord", "vec4(0.0)", "0", std::to_string(swizzleCode)}, "",
        readExpression);
    fragBuilder->appendCall(args.outputColor, mulEntry, {"deviceRead", args.inputColor}, "",
                            mulExpression);
    return;
  }
  fragBuilder->codeAppendf("highp vec3 deviceCoord = %s * vec3(gl_FragCoord.xy, 1.0);",
                           deviceCoordMatrixName.c_str());
  std::string coordName = "deviceCoord.xy";
  fragBuilder->codeAppendf("%s = ", args.outputColor.c_str());
  fragBuilder->appendTextureLookup((*args.textureSamplers)[0], coordName);
  fragBuilder->codeAppend(";");
  if (textureProxy->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = %s.a * %s;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  } else {
    fragBuilder->codeAppendf("%s = %s * %s.a;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  }
  if (args.irBuilder != nullptr && args.inputValue != INVALID_VALUE_ID &&
      args.outputValue != INVALID_VALUE_ID) {
    const auto samplerValue = args.samplerValue(sampler);
    if (samplerValue != INVALID_VALUE_ID) {
      const auto matrixValue = args.irBuilder->InternValue(deviceCoordMatrixName, SLType::Float3x3,
                                                           Precision::High, IRValueKind::Uniform);
      const auto fragCoordValue = args.irBuilder->CreateBuiltin("gl_FragCoord", SLType::Float4);
      const auto xyValue = args.irBuilder->CreateSwizzle(fragCoordValue, SLType::Float2, "xy");
      const auto deviceCoordValue = args.irBuilder->CreateCall(
          OperatorID::DeviceCoord, SLType::Float2, {matrixValue, xyValue}, "deviceCoord");
      const auto readValue =
          args.irBuilder->CreateTextureSample(samplerValue, deviceCoordValue, SLType::Float4,
                                              "deviceRead", SLType::Texture2DSampler, swizzleCode);
      const auto alphaValue = args.irBuilder->CreateSwizzle(readValue, SLType::Float, "a");
      const auto op =
          textureProxy->isAlphaOnly() ? OperatorID::AlphaModulate : OperatorID::MulAlpha;
      const auto result =
          args.irBuilder->CreateCall(static_cast<OperatorID>(op), SLType::Float4,
                                     {textureProxy->isAlphaOnly() ? alphaValue : readValue,
                                      textureProxy->isAlphaOnly() ? args.inputValue : alphaValue},
                                     args.outputColor);
      args.irBuilder->StoreOutput(args.outputValue, result);
    }
  }
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
