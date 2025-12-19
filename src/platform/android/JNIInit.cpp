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

#include "JNIInit.h"
#include "GlyphRenderer.h"
#include "HandlerThread.h"
#include "NativeCodec.h"
#include "platform/android/SurfaceTexture.h"
#include "tgfx/platform/android/AndroidBitmap.h"

namespace tgfx {
void JNIInit::Run() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return;
  }
  initialized = true;
  NativeCodec::JNIInit(env);
  HandlerThread::JNIInit(env);
  SurfaceTexture::JNIInit(env);
  AndroidBitmap::JNIInit(env);
  GlyphRenderer::JNIInit(env);
  env->ExceptionClear();
}
}  // namespace tgfx
