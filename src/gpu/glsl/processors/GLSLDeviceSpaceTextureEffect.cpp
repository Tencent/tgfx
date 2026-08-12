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

void GLSLDeviceSpaceTextureEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;
  auto deviceCoordMatrixName = uniformHandler->addUniform(
      "DeviceCoordMatrix", UniformFormat::Float3x3, ShaderStage::Fragment);
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
  // Shared precompiled kernels (a device mask alongside texture fills) declare a
  // DeviceMaskSubset decoy: this FP's Subset/AlphaOnly/Rect writes are dead or would clobber the
  // leaf processor's same-named fields, so they are routed there and skipped here. The decoy is
  // never read — these kernels sample the mask unclamped, matching the runtime emission.
  bool sharedKernel = fragmentUniformData->hasField("DeviceMaskSubset");
  if (sharedKernel) {
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    if (textureView->getTexture()->type() == TextureType::Rectangle) {
      rect[2] = static_cast<float>(textureView->width());
      rect[3] = static_cast<float>(textureView->height());
    }
    fragmentUniformData->setData("DeviceMaskSubset", rect);
  }
  if (!sharedKernel && fragmentUniformData->hasField("Subset")) {
    // Precompiled kernels declare Subset unconditionally and always clamp, so this source must
    // upload a no-op full-bounds rect: normalized for 2D textures, pixel bounds for Rectangle.
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    if (textureView->getTexture()->type() == TextureType::Rectangle) {
      rect[2] = static_cast<float>(textureView->width());
      rect[3] = static_cast<float>(textureView->height());
    }
    fragmentUniformData->setData("Subset", rect);
  }
  if (!sharedKernel && fragmentUniformData->hasField("AlphaOnly")) {
    // Precompiled path folds ALPHA_ONLY into a runtime uniform; JIT emitCode bakes it at compile
    // time and never declares this field.
    int alphaOnly = textureProxy->isAlphaOnly() ? 1 : 0;
    fragmentUniformData->setData("AlphaOnly", alphaOnly);
  }
  if (!sharedKernel && fragmentUniformData->hasField("Rect")) {
    // The shared coverage_output.inc always evaluates the AARect clip term when HAS_COVERAGE >= 1.
    // A bare device-space mask carries no AARectEffect, so upload a rect that covers the whole plane
    // — its clip coverage then evaluates to 1 everywhere, leaving only the mask sample. This keeps
    // the shared coverage path correct without a dedicated permutation dimension or uniform field.
    fragmentUniformData->setData("Rect", Rect::MakeLTRB(-1e9f, -1e9f, 1e9f, 1e9f));
  }
  if (!sharedKernel && fragmentUniformData->hasField("HasClip")) {
    int hasClip = 0;
    fragmentUniformData->setData("HasClip", hasClip);
  }
}
}  // namespace tgfx
