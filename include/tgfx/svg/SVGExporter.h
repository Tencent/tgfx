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

#include <memory>
#include <sstream>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Rect.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class SVGExportingContext;

struct ExportingOptions {
  ExportingOptions() = default;
  ExportingOptions(bool convertTextToPaths, bool prettyXML)
      : convertTextToPaths(convertTextToPaths), prettyXML(prettyXML) {
  }
  /**
   * Convert text to paths in the exported SVG text, only applicable to outline fonts. Emoji and
   * web fonts will only be exported as text.
   */
  bool convertTextToPaths = false;
  /**
   * Format SVG text with spaces('/t') and newlines('/n').
   */
  bool prettyXML = true;
};

/**
 * SVGExporter is a class used to export SVG text, converting drawing commands in the Canvas to
 * SVG text.
 */
class SVGExporter {
 public:
  /**
   * Creates an SVGExporter object pointer, which can be used to export SVG text.
   *
   * @param svgStream The string stream to store the SVG text.
   * @param context used to convert some rendering commands into image data.
   * @param viewBox viewBox of SVG, and content that exceeds the display area in the
   * SVG will be clipped.
   * @param options Options for exporting SVG text.
   */
  static std::shared_ptr<SVGExporter> Make(std::stringstream& svgStream, Context* context,
                                           const Rect& viewBox, ExportingOptions options);

  /**
   * Destroys the SVGExporter object, equivalent to calling close().
   */
  ~SVGExporter();

  /**
   * Get the canvas if the SVGExporter is not closed.if closed, return nullptr.
   */
  Canvas* getCanvas() const;

  /**
   * Closes the SVGExporter.Finalizing any unfinished drawing commands and writing the SVG end tag.
   */
  void close();

 private:
  /**
   * Construct a SVGExporter object
   */
  SVGExporter(std::stringstream& svgStream, Context* context, const Rect& viewBox,
              ExportingOptions options);

  bool closed = false;
  SVGExportingContext* drawContext = nullptr;
  Canvas* canvas = nullptr;
};

}  // namespace tgfx