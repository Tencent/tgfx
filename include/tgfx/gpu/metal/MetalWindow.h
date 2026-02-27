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

#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Window.h"
#include "tgfx/gpu/metal/MetalDevice.h"

namespace tgfx {
/**
 * MetalWindow is a Window implementation backed by a CAMetalLayer. It acquires a drawable from the
 * layer each frame and presents it after rendering.
 */
class MetalWindow : public Window {
 public:
  /**
   * Creates a new MetalWindow from a CAMetalLayer. The layer parameter is a pointer to a
   * CAMetalLayer object. If no device is provided, a new MetalDevice will be created. The caller
   * is responsible for configuring the layer properties (e.g., pixelFormat, drawableSize) before
   * creating the window.
   */
  static std::shared_ptr<MetalWindow> MakeFrom(void* layer,
                                               std::shared_ptr<MetalDevice> device = nullptr,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context) override;
  void onPresent(Context* context) override;
  void onFreeSurface() override;

 private:
  void* metalLayer = nullptr;
  void* currentDrawable = nullptr;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  MetalWindow(std::shared_ptr<Device> device, void* layer, std::shared_ptr<ColorSpace> colorSpace);
};

}  // namespace tgfx
