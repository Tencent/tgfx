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
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include "MetalCommandQueue.h"
#include "MetalGPU.h"
#include "core/utils/Log.h"
#include "gpu/proxies/DelayRenderTargetProxy.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/metal/MetalTypes.h"

namespace tgfx {

class MetalDrawableProvider : public RenderTargetProvider {
 public:
  explicit MetalDrawableProvider(CAMetalLayer* layer) : layer(layer) {
  }

  std::shared_ptr<RenderTarget> getRenderTarget(Context* context) override {
    drawable = [layer nextDrawable];
    if (drawable == nil) {
      return nullptr;
    }
    MetalTextureInfo metalInfo = {};
    metalInfo.texture = (__bridge const void*)drawable.texture;
    metalInfo.format = static_cast<unsigned>(drawable.texture.pixelFormat);
    auto width = static_cast<int>(drawable.texture.width);
    auto height = static_cast<int>(drawable.texture.height);
    BackendRenderTarget backendRT(metalInfo, width, height);
    return RenderTarget::MakeFrom(context, backendRT, ImageOrigin::TopLeft);
  }

  id<CAMetalDrawable> getDrawable() const {
    return drawable;
  }

 private:
  CAMetalLayer* layer = nil;
  id<CAMetalDrawable> drawable = nil;
};

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

std::shared_ptr<Surface> MetalWindow::onCreateSurface(Context* context) {
  if (metalView != nil) {
    metalLayer.drawableSize = metalView.drawableSize;
  }
  auto drawableSize = metalLayer.drawableSize;
  auto width = static_cast<int>(drawableSize.width);
  auto height = static_cast<int>(drawableSize.height);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  drawableProvider = std::make_shared<MetalDrawableProvider>(metalLayer);
  MetalTextureInfo formatInfo = {};
  formatInfo.format = static_cast<unsigned>(metalLayer.pixelFormat);
  BackendRenderTarget tempRT(formatInfo, width, height);
  auto format = tempRT.format();
  drawableProxy = std::make_shared<DelayRenderTargetProxy>(context, width, height, format,
                                                           ImageOrigin::TopLeft, drawableProvider);
  return Surface::MakeFrom(drawableProxy, 0, false, colorSpace);
}

void MetalWindow::onPresent(Context* context) {
  if (drawableProvider == nullptr) {
    return;
  }
  auto drawable = drawableProvider->getDrawable();
  if (drawable == nil) {
    return;
  }
  auto metalGPU = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(metalGPU->queue());
  auto commandBuffer = [queue->metalCommandQueue() commandBuffer];
  [commandBuffer presentDrawable:drawable];
  [commandBuffer commit];
  drawableProxy->reset();
}

void MetalWindow::onFreeSurface() {
  Window::onFreeSurface();
  if (drawableProxy != nullptr) {
    drawableProxy->reset();
  }
  drawableProxy = nullptr;
  drawableProvider = nullptr;
}

}  // namespace tgfx
