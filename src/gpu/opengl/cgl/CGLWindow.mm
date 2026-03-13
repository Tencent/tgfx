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
#include "gpu/opengl/cgl/CGLDrawable.h"
#include "platform/apple/CGColorSpaceUtil.h"

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
  if (colorSpace != nullptr && !ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    auto cgColorSpace = CreateCGColorSpace(colorSpace);
    view.window.colorSpace = [[NSColorSpace alloc] initWithCGColorSpace:cgColorSpace];
    CFRelease(cgColorSpace);
  }
  return std::shared_ptr<CGLWindow>(new CGLWindow(device, view, std::move(colorSpace)));
}

CGLWindow::CGLWindow(std::shared_ptr<Device> device, NSView* view,
                     std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)), view(view), colorSpace(std::move(colorSpace)) {
  // do not retain view here, otherwise it can cause circular reference.
}

CGLWindow::~CGLWindow() {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
  [glContext setView:nil];
  view = nil;
}

std::shared_ptr<Drawable> CGLWindow::onCreateDrawable(Context*) {
  auto glContext = static_cast<CGLDevice*>(device.get())->glContext;
  CGSize size = [view convertSizeToBacking:view.bounds.size];
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }
  auto width = static_cast<int>(size.width);
  auto height = static_cast<int>(size.height);
  return std::make_shared<CGLDrawable>(glContext, view, width, height, colorSpace);
}
}  // namespace tgfx

#pragma clang diagnostic pop
