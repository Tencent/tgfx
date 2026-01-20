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

#include <emscripten/html5_webgpu.h>
#include <webgpu/webgpu_cpp.h>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
/**
 * The WebGPU interface for drawing graphics.
 */
class WebGPUDevice : public Device {
 public:
  /**
   * Creates a WebGPUDevice from the id of an existing HTMLCanvasElement. Note that the WebGPU
   * device initialization is asynchronous. The returned device may not be ready immediately.
   * Calling lockContext() before the device is ready will return nullptr.
   * @param canvasID The id of the HTMLCanvasElement, e.g. "#canvas".
   * @param colorSpace The color space for rendering. If nullptr, defaults to sRGB.
   * @return A shared pointer to the WebGPUDevice, or nullptr if canvasID is empty.
   */
  static std::shared_ptr<WebGPUDevice> MakeFrom(const std::string& canvasID,
                                                std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Returns the wgpu::Surface associated with the canvas. Returns nullptr if the surface has not
   * been configured yet.
   */
  wgpu::Surface wgpuSurface() const {
    return surface;
  }

  /**
   * Returns the texture format of the surface.
   */
  wgpu::TextureFormat surfaceFormat() const {
    return textureFormat;
  }

  /**
   * Configures the surface with the given size. This should be called when the canvas size
   * changes. Returns true if the surface was reconfigured, false if the size is unchanged.
   * @param width The width of the surface in pixels.
   * @param height The height of the surface in pixels.
   */
  bool configureSurface(int width, int height);

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit WebGPUDevice(const std::string& canvasID);

  void requestAdapter();

  std::string canvasID;
  int surfaceWidth = 0;
  int surfaceHeight = 0;
  wgpu::Instance instance = nullptr;
  wgpu::Surface surface = nullptr;
  wgpu::TextureFormat textureFormat = wgpu::TextureFormat::BGRA8Unorm;
};
}  // namespace tgfx