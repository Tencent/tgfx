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
class SVGExportingContext;

/**
 * SVGExporter is a class used to export SVG text, converting drawing commands in the Canvas to
 * SVG text.
 */
class SVGExporter {
 public:
  enum ExportingOptions {
    /**
     * Convert text to paths in the exported SVG text, only applicable to outline fonts. Emoji and
     * web fonts will only be exported as text.
     */
    ConvertTextToPaths = 0x01,
    /**
     * Do not format SVG text with spaces('/t') and newlines('/n').
     */
    NoPrettyXML = 0x02,
  };

  SVGExporter() = default;
  ~SVGExporter();

  /**
   * Begin exporting SVG text and return a Canvas object for drawing SVG graphics.
   *
   * @param gpuContext used to convert some rendering commands into image data.
   * @param size The size sets the size of the SVG, and content that exceeds the display area in the
   * SVG will be clipped.
   * @param exportingOptions Options for exporting SVG text.
   * @return Canvas* 
   */
  Canvas* beginExporting(Context* gpuContext, const ISize& size, uint32_t exportingOptions = 0);

  /**
   * Returns the canvas if it is active, or nullptr.
   */
  Canvas* getCanvas() const;

  /**
   * Finish exporting and return the SVG text. If the canvas is not active, return an empty string.
   */
  std::string finishExportingAsString();

 private:
  bool actively = false;
  SVGExportingContext* context = nullptr;
  Canvas* canvas = nullptr;
  std::stringstream svgStream;
};

}  // namespace tgfx