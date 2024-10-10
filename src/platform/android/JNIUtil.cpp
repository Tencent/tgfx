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

#include "JNIUtil.h"
#include <pthread.h>
#include <mutex>
#include "core/utils/Log.h"

namespace tgfx {
jstring SafeToJString(JNIEnv* env, const std::string& text) {
  static Global<jclass> StringClass = env->FindClass("java/lang/String");
  static jmethodID StringConstructID =
      env->GetMethodID(StringClass.get(), "<init>", "([BLjava/lang/String;)V");
  auto textSize = static_cast<jsize>(text.size());
  auto array = env->NewByteArray(textSize);
  env->SetByteArrayRegion(array, 0, textSize, reinterpret_cast<const jbyte*>(text.c_str()));
  auto stringUTF = env->NewStringUTF("UTF-8");
  auto result = (jstring)env->NewObject(StringClass.get(), StringConstructID, array, stringUTF);
  env->DeleteLocalRef(array);
  env->DeleteLocalRef(stringUTF);
  return result;
}
}  // namespace tgfx
