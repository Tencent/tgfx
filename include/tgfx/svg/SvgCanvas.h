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

#include <memory>
#include <sstream>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * SvgCanvas provides an interface for creating a Canvas that writes SVG content to a stream.
 * it used to export the Canvas content to SVG format.
 */
class SvgCanvas {
 public:
  /**
   * Creates a new SvgCanvas instance with the specified bounds and string stream.
   * the bound will save to <svg> element's viewBox attribute.
   * If the draw calls are finished,the svg content written to the string stream.
   * the svg content can be obtained by calling writer.str(). 
   */
  static std::unique_ptr<Canvas> Make(const Rect& bounds, std::stringstream& writer);
};
}  // namespace tgfx
