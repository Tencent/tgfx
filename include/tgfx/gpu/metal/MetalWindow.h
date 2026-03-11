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

class MetalWindow : public Window {
 public:
  /**
   * Creates a new window from a CAMetalLayer with the specified device.
   * @param layer The CAMetalLayer to render into. Must not be nil.
   * @param device An optional MetalDevice. If nullptr, a new device is created from the layer's
   * existing device, or the system default device if the layer has none.
   * @param colorSpace An optional color space for rendering. If nullptr, the default sRGB is used.
   */
  static std::shared_ptr<MetalWindow> MakeFrom(CAMetalLayer* layer,
                                               std::shared_ptr<MetalDevice> device = nullptr,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates a new window from an MTKView with its underlying CAMetalLayer.
   * @param view The MTKView whose underlying CAMetalLayer is used for rendering. Must not be nil.
   * @param colorSpace An optional color space for rendering. If nullptr, the default sRGB is used.
   */
  static std::shared_ptr<MetalWindow> MakeFrom(MTKView* view,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

 protected:
  std::shared_ptr<Drawable> onCreateDrawable(Context* context) override;

 private:
  CAMetalLayer* metalLayer = nil;
  MTKView* metalView = nil;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  MetalWindow(std::shared_ptr<Device> device, CAMetalLayer* layer,
              std::shared_ptr<ColorSpace> colorSpace);
  MetalWindow(std::shared_ptr<Device> device, MTKView* view, CAMetalLayer* layer,
              std::shared_ptr<ColorSpace> colorSpace);
};

}  // namespace tgfx
