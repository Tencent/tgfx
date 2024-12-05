/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <vector>

namespace tgfx {
/**
 * EGLGlobals defines the global attributes for initializing EGL.
 */
class EGLGlobals {
 public:
  /**
   * Returns the current EGLGlobals instance.
   */
  static const EGLGlobals* Get();

  /**
   * Sets the EGLGlobals instance to user-defined value.
   */
  static void Set(const EGLGlobals* globals);

  EGLDisplay display = nullptr;
  EGLConfig windowConfig = nullptr;
  EGLConfig pbufferConfig = nullptr;
  std::vector<EGLint> windowSurfaceAttributes = {};
  std::vector<EGLint> pbufferSurfaceAttributes = {};
};
}  // namespace tgfx
