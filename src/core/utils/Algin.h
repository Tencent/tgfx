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

namespace tgfx {

template <typename T>
static constexpr T Align2(T x) {
  return (x + 1) >> 1 << 1;
}

template <typename T>
static constexpr T Align4(T x) {
  return (x + 3) >> 2 << 2;
}

template <typename T>
static constexpr T Align8(T x) {
  return (x + 7) >> 3 << 3;
}

template <typename T>
static constexpr bool IsAlign2(T x) {
  return 0 == (x & 1);
}

template <typename T>
static constexpr bool IsAlign4(T x) {
  return 0 == (x & 3);
}

template <typename T>
static constexpr bool IsAlign8(T x) {
  return 0 == (x & 7);
}

}  // namespace tgfx
