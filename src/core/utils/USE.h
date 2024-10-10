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

namespace tgfx {

/**
 * The USE(x, ...) template is used to silence C++ compiler warnings issued for (yet) unused
 * variables (typically parameters).
 */

#if defined(_MSC_VER)
#define USE(expr)                                                                         \
  __pragma(warning(push)) /* Disable warning C4127: conditional expression is constant */ \
      __pragma(warning(disable : 4127)) do {                                              \
    if (false) {                                                                          \
      (void)(expr);                                                                       \
    }                                                                                     \
  }                                                                                       \
  while (false) __pragma(warning(pop))
#else
#define USE(expr)   \
  do {              \
    if (false) {    \
      (void)(expr); \
    }               \
  } while (false)
#endif

}  // namespace tgfx
