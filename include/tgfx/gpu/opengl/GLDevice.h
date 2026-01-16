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

#pragma once

#include "tgfx/gpu/Device.h"

namespace tgfx {
/**
 * The OpenGL interface for drawing graphics.
 */
class GLDevice : public Device {
 public:
  /**
   * Returns the native handle of current OpenGL context on the calling thread.
   */
  static void* CurrentNativeHandle();

  /**
   * Returns an GLDevice associated with current OpenGL context. Returns nullptr if there is no
   * current OpenGL context on the calling thread.
   */
  static std::shared_ptr<GLDevice> Current();

  /**
   * Creates a new GLDevice with specified shared OpenGL context.
   */
  static std::shared_ptr<GLDevice> Make(void* sharedContext = nullptr);

  /**
   * Creates a new GLDevice. If the creation fails, it will return a pre-created GLDevice. If that
   * also fails, it will return the active GLDevice of the current thread. If all attempts fail,
   * nullptr will be returned.
   */
  static std::shared_ptr<GLDevice> MakeWithFallback();

  /**
   * Returns the GLDevice associated with the specified OpenGL context.
   */
  static std::shared_ptr<GLDevice> Get(void* nativeHandle);

  ~GLDevice() override;

  /**
   * Returns true if the specified native handle is shared context to this GLDevice.
   */
  virtual bool sharableWith(void* nativeHandle) const = 0;

 protected:
  void* nativeHandle = nullptr;
  bool externallyOwned = false;

  GLDevice(std::unique_ptr<GPU> gpu, void* nativeHandle);

  void releaseAll();

  friend class GLContext;
};
}  // namespace tgfx
