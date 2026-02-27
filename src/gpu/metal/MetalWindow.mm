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
#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include "core/utils/Log.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/metal/MetalTypes.h"

namespace tgfx {

std::shared_ptr<MetalWindow> MetalWindow::MakeFrom(void* layer, std::shared_ptr<MetalDevice> device,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (layer == nullptr) {
    return nullptr;
  }
  auto metalLayer = (__bridge CAMetalLayer*)layer;
  if (![metalLayer isKindOfClass:[CAMetalLayer class]]) {
    LOGE("MetalWindow::MakeFrom() The layer parameter is not a CAMetalLayer.");
    return nullptr;
  }
  if (device == nullptr) {
    if (metalLayer.device != nil) {
      device = MetalDevice::MakeFrom((__bridge void*)metalLayer.device);
    } else {
      device = MetalDevice::Make();
    }
  }
  if (device == nullptr) {
    return nullptr;
  }
  if (metalLayer.device == nil) {
    metalLayer.device = (__bridge id<MTLDevice>)device->metalDevice();
  }
  return std::shared_ptr<MetalWindow>(new MetalWindow(device, layer, std::move(colorSpace)));
}

MetalWindow::MetalWindow(std::shared_ptr<Device> device, void* layer,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)), metalLayer(layer), colorSpace(std::move(colorSpace)) {
}

std::shared_ptr<Surface> MetalWindow::onCreateSurface(Context* context) {
  auto layer = (__bridge CAMetalLayer*)metalLayer;
  auto drawable = [layer nextDrawable];
  if (drawable == nil) {
    return nullptr;
  }
  [drawable retain];
  currentDrawable = (__bridge void*)drawable;
  id<MTLTexture> texture = drawable.texture;
  auto width = static_cast<int>(texture.width);
  auto height = static_cast<int>(texture.height);
  MetalTextureInfo metalInfo = {};
  metalInfo.texture = (__bridge const void*)texture;
  metalInfo.format = static_cast<unsigned>(texture.pixelFormat);
  BackendRenderTarget renderTarget(metalInfo, width, height);
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::TopLeft, 0, colorSpace);
}

void MetalWindow::onPresent(Context*) {
  if (currentDrawable == nullptr) {
    return;
  }
  auto drawable = (__bridge id<CAMetalDrawable>)currentDrawable;
  [drawable present];
  [drawable release];
  currentDrawable = nullptr;
}

void MetalWindow::onFreeSurface() {
  Window::onFreeSurface();
  if (currentDrawable != nullptr) {
    auto drawable = (__bridge id<CAMetalDrawable>)currentDrawable;
    [drawable release];
    currentDrawable = nullptr;
  }
}

}  // namespace tgfx
