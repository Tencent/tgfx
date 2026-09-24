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

#include <emscripten/val.h>
#include "tgfx/gpu/Device.h"

namespace tgfx {

/**
 * The WebGPU interface for drawing graphics.
 */
class WebGPUDevice : public Device {
 public:
  /**
   * Creates a new WebGPUDevice by requesting the default adapter and device.
   */
  static std::shared_ptr<WebGPUDevice> Make();

  /**
   * Creates a new WebGPUDevice from an existing WGPUDevice. The device parameter is a pointer to a
   * WGPUDevice object. The caller retains ownership of the device and must keep it alive for the
   * lifetime of the returned WebGPUDevice. tgfx will NOT release the device on shutdown.
   * On Web, the handle must be a device registered in the emscripten WebGPU runtime (e.g. from
   * emscripten_webgpu_get_device() or emscripten_webgpu_import_device()).
   * Note: This method sets the device's uncaptured error callback for internal error reporting. The
   * WebGPU spec provides only a single-slot callback, so any previously set callback will be
   * overwritten. The callback is not restored on destruction.
   */
  static std::shared_ptr<WebGPUDevice> MakeFrom(void* device);

  /**
   * Creates a new WebGPUDevice from an existing GPUDevice object, such as one obtained from
   * navigator.gpu.
   *
   * Use this on a thread that cannot obtain the default device, such as a worker thread. The device
   * belongs to the calling thread: it has to be rendered with and destroyed on that thread, and it
   * has to stay alive for as long as the returned WebGPUDevice does. tgfx will NOT release it on
   * shutdown.
   *
   * On Web, the final executable must export the WebGPU runtime method (see the Web build section
   * in README.md); without it the device cannot be created.
   *
   * @param device A GPUDevice. Returns nullptr if it is null.
   */
  static std::shared_ptr<WebGPUDevice> MakeFrom(emscripten::val device);

  ~WebGPUDevice() override;

  /**
   * Returns the underlying WGPUDevice as a void pointer.
   */
  void* webgpuDevice() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit WebGPUDevice(std::unique_ptr<class WebGPUGPU> gpu);
};

}  // namespace tgfx
