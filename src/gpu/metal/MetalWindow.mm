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

#include "tgfx/gpu/metal/MetalWindow.h"
#import <MetalKit/MetalKit.h>
#include "gpu/metal/MetalDrawable.h"
#include "platform/apple/CGColorSpaceUtil.h"

namespace tgfx {

static void ApplyColorSpace(CAMetalLayer* layer, const std::shared_ptr<ColorSpace>& colorSpace) {
  if (colorSpace != nullptr && !ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    auto cgColorSpace = CreateCGColorSpace(colorSpace);
    layer.colorspace = cgColorSpace;
    CFRelease(cgColorSpace);
  }
}

std::shared_ptr<MetalWindow> MetalWindow::MakeFrom(CAMetalLayer* layer,
                                                   std::shared_ptr<MetalDevice> device,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (layer == nil) {
    return nullptr;
  }
  if (device == nullptr) {
    if (layer.device != nil) {
      device = MetalDevice::MakeFrom((__bridge void*)layer.device);
    } else {
      device = MetalDevice::Make();
    }
  }
  if (device == nullptr) {
    return nullptr;
  }
  if (layer.device == nil) {
    layer.device = (__bridge id<MTLDevice>)device->metalDevice();
  }
  ApplyColorSpace(layer, colorSpace);
  return std::shared_ptr<MetalWindow>(new MetalWindow(device, layer, std::move(colorSpace)));
}

std::shared_ptr<MetalWindow> MetalWindow::MakeFrom(MTKView* view,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (view == nil) {
    return nullptr;
  }
  auto layer = static_cast<CAMetalLayer*>(view.layer);
  if (layer == nil) {
    return nullptr;
  }
  auto device = layer.device != nil ? MetalDevice::MakeFrom((__bridge void*)layer.device)
                                    : MetalDevice::Make();
  if (device == nullptr) {
    return nullptr;
  }
  if (layer.device == nil) {
    layer.device = (__bridge id<MTLDevice>)device->metalDevice();
  }
  ApplyColorSpace(layer, colorSpace);
  return std::shared_ptr<MetalWindow>(new MetalWindow(device, view, layer, std::move(colorSpace)));
}

// Do not retain layer/view here, otherwise it can cause circular reference.
MetalWindow::MetalWindow(std::shared_ptr<Device> device, CAMetalLayer* layer,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)), metalLayer(layer), colorSpace(std::move(colorSpace)) {
}

MetalWindow::MetalWindow(std::shared_ptr<Device> device, MTKView* view, CAMetalLayer* layer,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)),
      metalLayer(layer),
      metalView(view),
      colorSpace(std::move(colorSpace)) {
}

std::shared_ptr<Drawable> MetalWindow::onCreateDrawable(Context*) {
  if (metalView != nil) {
    metalLayer.drawableSize = metalView.drawableSize;
  }
  auto drawableSize = metalLayer.drawableSize;
  auto width = static_cast<int>(drawableSize.width);
  auto height = static_cast<int>(drawableSize.height);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::make_shared<MetalDrawable>(metalLayer, width, height, colorSpace);
}

}  // namespace tgfx
