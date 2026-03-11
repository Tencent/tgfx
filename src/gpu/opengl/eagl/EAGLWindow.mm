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
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/eagl/EAGLDrawable.h"
#include "gpu/opengl/eagl/EAGLLayerTexture.h"

namespace tgfx {
std::shared_ptr<EAGLWindow> EAGLWindow::MakeFrom(CAEAGLLayer* layer,
                                                 std::shared_ptr<GLDevice> device,
                                                 std::shared_ptr<ColorSpace> colorSpace) {
  if (layer == nil) {
    return nullptr;
  }
  if (device == nullptr) {
    device = GLDevice::MakeWithFallback();
  }
  if (device == nullptr) {
    return nullptr;
  }
  if (colorSpace != nullptr && !ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    LOGE("EAGLWindow::MakeFrom() The specified ColorSpace is not supported on this platform. "
         "Rendering may have color inaccuracies.");
  }
  return std::shared_ptr<EAGLWindow>(new EAGLWindow(device, layer, std::move(colorSpace)));
}

EAGLWindow::EAGLWindow(std::shared_ptr<Device> device, CAEAGLLayer* layer,
                       std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)), layer(layer), colorSpace(std::move(colorSpace)) {
  // do not retain layer here, otherwise it can cause circular reference.
}

std::shared_ptr<Drawable> EAGLWindow::onCreateDrawable(Context* context) {
  auto width = static_cast<int>(layer.bounds.size.width * layer.contentsScale);
  auto height = static_cast<int>(layer.bounds.size.height * layer.contentsScale);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  // Release old layer texture to unbind the renderbuffer from the layer before creating a new one.
  if (layerTexture != nullptr) {
    layerTexture->release(static_cast<GLGPU*>(context->gpu()));
    layerTexture = nullptr;
  }
  layerTexture = EAGLLayerTexture::MakeFrom(static_cast<GLGPU*>(context->gpu()), layer);
  if (layerTexture == nullptr) {
    return nullptr;
  }
  return std::make_shared<EAGLDrawable>(layerTexture, width, height, colorSpace);
}
}  // namespace tgfx
