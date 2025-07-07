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

#include <memory>
#include "JNIUtil.h"

namespace tgfx {
class HandlerThread {
 public:
  static void JNIInit(JNIEnv* env);

  static std::shared_ptr<HandlerThread> Make();

  HandlerThread(JNIEnv* env, jobject thread);

  ~HandlerThread();

  jobject getLooper() const {
    return looper.get();
  }

 private:
  Global<jobject> thread;
  Global<jobject> looper;
};
}  // namespace tgfx
