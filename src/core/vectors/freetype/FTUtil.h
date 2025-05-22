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

#include <array>
#include <memory>
#include "ft2build.h"
#include FT_FREETYPE_H

namespace tgfx {
inline FT_F26Dot6 FloatToFDot6(float x) {
  return static_cast<FT_F26Dot6>(x * 64.f);
}

inline float FDot6ToFloat(FT_F26Dot6 x) {
  return static_cast<float>(x) * 0.015625f;
}

inline FT_F26Dot6 FDot6Floor(FT_F26Dot6 x) {
  return x >> 6;
}

inline FT_F26Dot6 FDot6Ceil(FT_F26Dot6 x) {
  return ((x) + 63) >> 6;
}

inline FT_F26Dot6 FDot6Round(FT_F26Dot6 x) {
  return (((x) + 32) >> 6);
}

struct RasterTarget {
  unsigned char* origin;
  int pitch;
  const uint8_t* gammaTable;
};

void GraySpanFunc(int y, int count, const FT_Span* spans, void* user);
}  // namespace tgfx
