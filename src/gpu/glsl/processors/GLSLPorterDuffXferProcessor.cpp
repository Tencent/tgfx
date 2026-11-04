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
  // A runtime-compiled program whose fragment stage declares no uniforms hands over a null block
  // (the JIT bakes the blend mode into the shader code and a no-dst-texture draw declares nothing
  // else), so every field access must be guarded.
  if (fragmentUniformData == nullptr) {
    return;
  }
  // For precompiled shaders, blend mode is passed as a uniform rather than baked into shader code.
  // This must be set before the dst-texture early return below: the framebuffer-fetch path has no
  // dst texture yet still reads XPBlendMode, so setting it afterwards would leave it unwritten and
  // the FBF blend would use a stale mode.
  if (fragmentUniformData->hasField("XPBlendMode")) {
    int mode = static_cast<int>(blendMode);
    fragmentUniformData->setData("XPBlendMode", mode);
  }
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
}  // namespace tgfx
