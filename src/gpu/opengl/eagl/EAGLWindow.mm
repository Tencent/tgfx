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

#include "tgfx/gpu/opengl/eagl/EAGLWindow.h"
#include "core/utils/Log.h"
#include "gpu/opengl/eagl/EAGLLayerTexture.h"
#include "tgfx/gpu/opengl/GLFunctions.h"

namespace tgfx {
std::shared_ptr<EAGLWindow> EAGLWindow::MakeFrom(CAEAGLLayer* layer,
                                                 std::shared_ptr<GLDevice> device) {
  if (layer == nil) {
    return nullptr;
  }
  if (device == nullptr) {
    device = GLDevice::MakeWithFallback();
  }
  if (device == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<EAGLWindow>(new EAGLWindow(device, layer));
}

EAGLWindow::EAGLWindow(std::shared_ptr<Device> device, CAEAGLLayer* layer)
    : Window(std::move(device)), layer(layer) {
  // do not retain layer here, otherwise it can cause circular reference.
}

std::shared_ptr<Surface> EAGLWindow::onCreateSurface(Context* context) {
  if (layerTexture != nullptr) {
    layerTexture = nullptr;
  }
  layerTexture = EAGLLayerTexture::MakeFrom(static_cast<GLGPU*>(context->gpu()), layer);
  if (layerTexture == nullptr) {
    return nullptr;
  }
  BackendRenderTarget renderTarget = layerTexture->getBackendRenderTarget();
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
}

void EAGLWindow::onPresent(Context* context, int64_t) {
  auto gl = GLFunctions::Get(context);
  gl->bindRenderbuffer(GL_RENDERBUFFER, layerTexture->colorBufferID());
  auto eaglContext = static_cast<EAGLDevice*>(context->device())->eaglContext();
  [eaglContext presentRenderbuffer:GL_RENDERBUFFER];
  gl->bindRenderbuffer(GL_RENDERBUFFER, 0);
}
}  // namespace tgfx
