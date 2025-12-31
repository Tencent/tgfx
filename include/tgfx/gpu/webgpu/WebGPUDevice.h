/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
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
   * Creates a WebGLDevice from the id of an existing HTMLCanvasElement.
   */
  static std::shared_ptr<WebGPUDevice> MakeFrom(const std::string& canvasID,
                                                std::shared_ptr<ColorSpace> colorSpace = nullptr);

 private:
  WebGPUDevice();

  void requestAdapter();

  wgpu::Instance instance = nullptr;
};
}  // namespace tgfx