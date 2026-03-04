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

#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

@class MTKView;
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Window.h"
#include "tgfx/gpu/metal/MetalDevice.h"

namespace tgfx {
/**
 * MetalWindow is a Window implementation backed by a CAMetalLayer.
 */
class MetalWindow : public Window {
 public:
  /**
   * Creates a new window from a CAMetalLayer with the specified device. If no device is provided,
   * a new MetalDevice will be created automatically. The caller is responsible for configuring the
   * layer properties (e.g., pixelFormat, drawableSize) before creating the window.
   */
  static std::shared_ptr<MetalWindow> MakeFrom(CAMetalLayer* layer,
                                               std::shared_ptr<MetalDevice> device = nullptr,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a new window from an MTKView with its underlying CAMetalLayer. Returns nullptr if the
   * view or its layer is invalid.
   */
  static std::shared_ptr<MetalWindow> MakeFrom(MTKView* view,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ~MetalWindow() override;

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context) override;
  void onPresent(Context* context) override;
  void onFreeSurface() override;

 private:
  CAMetalLayer* metalLayer = nil;
  MTKView* metalView = nil;
  id<MTLTexture> offscreenTexture = nil;
  id<MTLRenderPipelineState> copyPipelineState = nil;
  MTLPixelFormat copyPixelFormat = MTLPixelFormatInvalid;
  int offscreenWidth = 0;
  int offscreenHeight = 0;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  MetalWindow(std::shared_ptr<Device> device, CAMetalLayer* layer,
              std::shared_ptr<ColorSpace> colorSpace);
  MetalWindow(std::shared_ptr<Device> device, MTKView* view, CAMetalLayer* layer,
              std::shared_ptr<ColorSpace> colorSpace);

  void blitToDrawable(id<MTLCommandBuffer> commandBuffer, id<CAMetalDrawable> drawable);
  void renderCopyToDrawable(id<MTLCommandBuffer> commandBuffer, id<CAMetalDrawable> drawable);
  void ensureCopyPipelineState(id<MTLDevice> device, MTLPixelFormat pixelFormat);
};

}  // namespace tgfx
