/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <cuchar>
#include <memory>
#include <string>
#include <tuple>
#include "tgfx/core/Color.h"
#include "tgfx/core/Path.h"

namespace tgfx {

class SVGParse {
 public:
  static const char* FindHex(const char str[], uint32_t* value);
  static bool FindNamedColor(const std::string& str, Color* color);
  static const char* FindS32(const char str[], int32_t* value);
  static const char* FindScalar(const char str[], float* value);
  static const char* FindScalars(const char str[], float value[], int count);
  static bool FindBool(const char str[], bool* value);
  // return the index of str in list[], or -1 if not found
  static int FindList(const char target[], const char list[]);

  static inline Color Uint32ToColor(uint32_t value) {
    return Color::FromRGBA((value >> 16) & 0xff, (value >> 8) & 0xff, (value >> 0) & 0xff,
                           (value >> 24) & 0xff);
  }
};

class PathParse {
 public:
  enum class PathEncoding { Absolute, Relative };

  static std::tuple<bool, std::shared_ptr<Path>> FromSVGString(const char data[]);

  static std::string ToSVGString(const Path& path, PathEncoding = PathEncoding::Absolute);
};
}  // namespace tgfx