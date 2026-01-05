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

#if defined(__EMSCRIPTEN__)
#include <emscripten/val.h>
#elif defined(__ANDROID__) || defined(ANDROID)

class _jobject;

#elif defined(__APPLE__)

struct CGImage;

#endif

namespace tgfx {
#if defined(__EMSCRIPTEN__)

typedef emscripten::val NativeImageRef;

#elif defined(__ANDROID__) || defined(ANDROID)

typedef _jobject* NativeImageRef;

#elif defined(__APPLE__)

typedef CGImage* NativeImageRef;

#else

struct NativeImage {};

typedef NativeImage* NativeImageRef;

#endif
}  // namespace tgfx
