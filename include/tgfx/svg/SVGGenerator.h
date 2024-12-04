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

#include <sstream>
#include "tgfx/core/Canvas.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class SVGContext;

/**
 * SVGGenerator is a class used to generate SVG text, converting drawing commands in the Canvas to
 * SVG text.
 */
class SVGGenerator {
 public:
  SVGGenerator() = default;
  ~SVGGenerator();

  /**
   * Begin generating SVG text and return a Canvas object for drawing SVG graphics.
   *
   * @param GPUContext used to convert some rendering commands into image data.
   * @param size The size sets the size of the SVG, and content that exceeds the display area in the
   * SVG will be clipped.
   * @param isPretty Whether to format the generated SVG text with "/n"、"/t"".
   * @return Canvas* 
   */
  Canvas* beginGenerate(Context* GPUContext, const ISize& size, bool isPretty = true);

  /**
   * Returns the recording canvas if one is active, or nullptr if recording is not active.
   */
  Canvas* getCanvas() const;

  /**
   * Finish generating SVG text and return the generated SVG text. If no recording is active.
   */
  std::string finishGenerate();

 private:
  bool _actively = false;
  SVGContext* _context = nullptr;
  Canvas* _canvas = nullptr;
  std::stringstream _svgStream;
};

}  // namespace tgfx