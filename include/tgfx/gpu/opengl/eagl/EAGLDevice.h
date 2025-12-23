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

#import <UIKit/UIKit.h>
#include "tgfx/gpu/opengl/GLDevice.h"

namespace tgfx {
class EAGLDevice : public GLDevice {
 public:
  /**
   * Creates an EAGLDevice with an existing EAGLContext.
   */
  static std::shared_ptr<EAGLDevice> MakeFrom(EAGLContext* eaglContext);

  ~EAGLDevice() override;

  bool sharableWith(void* nativeHandle) const override;

  EAGLContext* eaglContext() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  EAGLContext* _eaglContext = nil;
  EAGLContext* oldContext = nil;
  size_t cacheArrayIndex = 0;

  static std::shared_ptr<EAGLDevice> Wrap(EAGLContext* eaglContext, bool externallyOwned);
  static void NotifyReferenceReachedZero(EAGLDevice* device);

  EAGLDevice(std::unique_ptr<GPU> gpu, EAGLContext* eaglContext);
  bool makeCurrent(bool force = false);
  void clearCurrent();
  void finish();

  friend class GLDevice;
  friend class EAGLWindow;
  friend class EAGLHardwareTexture;

  friend void ApplicationDidEnterBackground();
  friend void ApplicationWillEnterForeground();
};
}  // namespace tgfx
