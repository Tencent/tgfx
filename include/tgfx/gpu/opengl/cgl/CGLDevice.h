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

#include <AppKit/AppKit.h>
#include "tgfx/gpu/opengl/GLDevice.h"

namespace tgfx {
class CGLDevice : public GLDevice {
 public:
  /**
   * Creates a CGLDevice with an existing CGLContext.
   */
  static std::shared_ptr<CGLDevice> MakeFrom(CGLContextObj cglContext);

  ~CGLDevice() override;

  bool sharableWith(void* nativeHandle) const override;

  CGLContextObj cglContext() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  NSOpenGLContext* glContext = nil;
  CGLContextObj oldContext = nil;

  static std::shared_ptr<CGLDevice> Wrap(CGLContextObj cglContext, bool externallyOwned);

  CGLDevice(std::unique_ptr<GPU> gpu, CGLContextObj cglContext);

  friend class GLDevice;
  friend class CGLWindow;
  friend class Texture;
};
}  // namespace tgfx
