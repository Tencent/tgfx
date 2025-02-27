/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <windows.h>
#include "WGLExtensions.h"

namespace tgfx {
class WGLContext {
 public:
  explicit WGLContext(HGLRC sharedContext);

  virtual ~WGLContext() = default;

  bool makeCurrent();

  void clearCurrent();

  HDC getDeviceContext() const {
    return deviceContext;
  }

  HGLRC getGLContext() const {
    return glContext;
  }

  HGLRC getSharedContext() const {
    return sharedContext;
  }

 protected:
  HDC deviceContext = nullptr;
  HGLRC glContext = nullptr;
  HGLRC sharedContext = nullptr;

  HDC oldDeviceContext = nullptr;
  HGLRC oldGLContext = nullptr;

  WGLExtensions extensions;

  void initializeContext();
  void destroyContext();

  virtual void onDestroyContext() = 0;
  virtual void onInitializeContext() = 0;
};
}  //namespace tgfx