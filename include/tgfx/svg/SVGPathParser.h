/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <string>
#include "tgfx/core/Path.h"

namespace tgfx {

class SVGPathParser {
 public:
  /**
   * Two ways to describe paths in SVG
   */
  enum class PathEncoding {
    /**
     * Each step's point is an absolute coordinate, and the step letter is uppercase
     */
    Absolute,
    /**
     * Each step's point is a relative coordinate to the previous point, and the step letter is
     *lowercase
     */
    Relative,
  };

  /**
   *  Parses a Path object to an SVG path string.
   */
  static std::string ToSVGString(const Path& path, PathEncoding = PathEncoding::Absolute);

  /**
   * Parses an SVG path string to a Path object.
   * Returns nullptr if the parsing fails.
   */
  static std::shared_ptr<Path> FromSVGString(const std::string& pathString);
};

}  // namespace tgfx
