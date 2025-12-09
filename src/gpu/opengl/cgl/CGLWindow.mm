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

#include "tgfx/gpu/opengl/cgl/CGLWindow.h"
#include <thread>
#include "core/utils/Log.h"
#include "gpu/opengl/GLDefines.h"
#include "tgfx/gpu/Backend.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace tgfx {

std::shared_ptr<CGLWindow> CGLWindow::MakeFrom(NSView* view, CGLContextObj sharedContext,
                                               std::shared_ptr<ColorSpace> colorSpace) {
  if (view == nil) {
    return nullptr;
  }
  auto device = GLDevice::Make(sharedContext);
  if (device == nullptr) {
    return nullptr;
  }
  if(colorSpace != nullptr && !ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())){
      LOGE("CGLWindow::MakeFrom() The specified ColorSpace is not supported on this platform. Rendering may have color inaccuracies.");
  }
  return std::shared_ptr<CGLWindow>(new CGLWindow(device, view, std::move(colorSpace)));
}

CGLWindow::CGLWindow(std::shared_ptr<Device> device, NSView* view,
                     std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device), std::move(colorSpace)), view(view) {
  // do not retain view here, otherwise it can cause circular reference.
}

CGLWindow::~CGLWindow() {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
  [glContext setView:nil];
  view = nil;
}

std::shared_ptr<Surface> CGLWindow::onCreateSurface(Context* context) {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
  [glContext update];
  CGSize size = [view convertSizeToBacking:view.bounds.size];
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }
  [glContext setView:view];
  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget renderTarget(frameBuffer, static_cast<int>(size.width),
                                   static_cast<int>(size.height));
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft, 0, colorSpace);
}

void CGLWindow::onPresent(Context*) {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
  [glContext flushBuffer];
}
}  // namespace tgfx

#pragma clang diagnostic pop