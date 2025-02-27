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

#include "WGLContext.h"
#include "core/utils/Log.h"

namespace tgfx {

WGLContext::WGLContext(HGLRC sharedContext) : sharedContext(sharedContext) {
}

void WGLContext::initializeContext() {
  DEBUG_ASSERT(!deviceContext);
  DEBUG_ASSERT(!glContext);
  onInitializeContext();
}

void WGLContext::destroyContext() {
  onDestroyContext();
}

bool WGLContext::makeCurrent() {
  oldGLContext = wglGetCurrentContext();
  oldDeviceContext = wglGetCurrentDC();
  if (oldGLContext == glContext) {
    return true;
  }
  if (!wglMakeCurrent(deviceContext, glContext)) {
    return false;
  }
  return true;
}

void WGLContext::clearCurrent() {
  if (oldGLContext == glContext) {
    return;
  }
  wglMakeCurrent(deviceContext, nullptr);
  if (oldDeviceContext != nullptr) {
    wglMakeCurrent(oldDeviceContext, oldGLContext);
  }
}
}  // namespace tgfx