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

#include "GLSLPorterDuffXferProcessor.h"

namespace tgfx {

PlacementPtr<PorterDuffXferProcessor> PorterDuffXferProcessor::Make(BlockAllocator* allocator,
                                                                    BlendMode blend,
                                                                    DstTextureInfo dstTextureInfo) {
  return allocator->make<GLSLPorterDuffXferProcessor>(blend, std::move(dstTextureInfo));
}

GLSLPorterDuffXferProcessor::GLSLPorterDuffXferProcessor(BlendMode blend,
                                                         DstTextureInfo dstTextureInfo)
    : PorterDuffXferProcessor(blend, std::move(dstTextureInfo)) {
}

void GLSLPorterDuffXferProcessor::setData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  if (dstTextureInfo.textureProxy == nullptr) {
    return;
  }
  auto dstTextureView = dstTextureInfo.textureProxy->getTextureView();
  if (dstTextureView == nullptr) {
    return;
  }
  fragmentUniformData->setData("DstTextureUpperLeft", dstTextureInfo.offset);
  int width;
  int height;
  if (dstTextureView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  } else {
    width = dstTextureView->width();
    height = dstTextureView->height();
  }
  float scales[] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  fragmentUniformData->setData("DstTextureCoordScale", scales);
}

ShaderCallResult PorterDuffXferProcessor::buildXferCallStatement(
    const std::string& colorInVar, const std::string& coverageInVar, const std::string& outputVar,
    const std::string& dstColorExpr, const MangledUniforms& uniforms,
    const MangledSamplers& samplers) const {
  ShaderCallResult result;
  result.outputVarName = outputVar;
  std::string args = colorInVar + ", " + coverageInVar;
  if (dstTextureInfo.textureProxy) {
    args += ", " + uniforms.get("DstTextureUpperLeft");
    args += ", " + uniforms.get("DstTextureCoordScale");
    args += ", " + samplers.get("DstTextureSampler");
  } else {
    args += ", " + dstColorExpr;
  }
  result.statement = "TGFX_PorterDuffXP_FS(" + args + ", " + outputVar + ");\n";
  return result;
}
}  // namespace tgfx
