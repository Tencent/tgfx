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

#if defined __SSE2__ || defined _M_AMD64 || (defined _M_IX86_FP && _M_IX86_FP == 2)
#include <emmintrin.h>
#else
#include <thread>
#endif

#pragma once

namespace inspector {

static void YieldThread() {
#if defined __SSE2__ || defined _M_AMD64 || (defined _M_IX86_FP && _M_IX86_FP == 2)
  _mm_pause();
#elif defined __aarch64__
  asm volatile("isb" : :);
#else
  std::this_thread::yield();
#endif
}
}  // namespace inspector