/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "EAGLDrawable.h"
#include "gpu/opengl/GLDefines.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/eagl/EAGLLayerTexture.h"
#include "tgfx/gpu/opengl/eagl/EAGLDevice.h"

namespace tgfx {
EAGLDrawable::EAGLDrawable(std::weak_ptr<EAGLLayerTexture> layerTexture, int width, int height,
                           std::shared_ptr<ColorSpace> colorSpace)
    : GLDrawable(width, height, std::move(colorSpace)), layerTexture(std::move(layerTexture)) {
}

BackendRenderTarget EAGLDrawable::onCreateBackendRenderTarget() {
  auto texture = layerTexture.lock();
  if (texture == nullptr) {
    return {};
  }
  return texture->getBackendRenderTarget();
}

void EAGLDrawable::onPresent(Context* context, std::shared_ptr<CommandBuffer>) {
  auto texture = layerTexture.lock();
  if (texture == nullptr) {
    return;
  }
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  gl->bindRenderbuffer(GL_RENDERBUFFER, texture->colorBufferID());
  auto eaglContext = static_cast<EAGLDevice*>(context->device())->eaglContext();
  [eaglContext presentRenderbuffer:GL_RENDERBUFFER];
  gl->bindRenderbuffer(GL_RENDERBUFFER, 0);
}
}  // namespace tgfx
