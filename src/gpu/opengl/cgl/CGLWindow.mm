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
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/opengl/GLDefines.h"

namespace tgfx {
std::shared_ptr<CGLWindow> CGLWindow::MakeFrom(NSView* view, CGLContextObj sharedContext) {
  if (view == nil) {
    return nullptr;
  }
  auto device = GLDevice::Make(sharedContext);
  if (device == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<CGLWindow>(new CGLWindow(device, view));
}

CGLWindow::CGLWindow(std::shared_ptr<Device> device, NSView* view)
    : Window(std::move(device)), view(view) {
  // do not retain view here, otherwise it can cause circular reference.
}

CGLWindow::~CGLWindow() {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [glContext setView:nil];
#pragma clang diagnostic pop
  view = nil;
}

std::shared_ptr<Surface> CGLWindow::onCreateSurface(Context* context) {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
  [glContext update];
  CGSize size = [view convertSizeToBacking:view.bounds.size];
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  [glContext setView:view];
#pragma clang diagnostic pop
  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget renderTarget(frameBuffer, static_cast<int>(size.width),
                                   static_cast<int>(size.height));
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
}

void CGLWindow::onPresent(Context*, int64_t) {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
  [glContext flushBuffer];
}
}  // namespace tgfx
