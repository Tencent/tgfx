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

/**
 * Options for exporting SVG text.
 */
struct SVGExportingOptions {
  /**
   * Construct Exporting Options object,default convertTextToPaths is false, prettyXML is true.
   */
  explicit SVGExportingOptions(bool convertTextToPaths = false, bool prettyXML = true)
      : convertTextToPaths(convertTextToPaths), prettyXML(prettyXML) {
  }
  /**
   * Convert text to paths in the exported SVG. This only applies to fonts with outlines. Fonts
   * without outlines, like emoji and web fonts, will be exported as text.
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
   * Creates an SVG exporter object pointer, which can be used to export SVG text.
   *
   * @param svgStream The string stream to store the SVG text.
   * @param context used to convert some rendering commands into image data.
   * @param viewBox viewBox of SVG, and content that exceeds the display area in the
   * SVG will be clipped.
   * @param options Options for exporting SVG text.
   */
  static std::shared_ptr<SVGExporter> Make(std::stringstream& svgStream, Context* context,
                                           const Rect& viewBox, SVGExportingOptions options);

  /**
   * Destroys the SVG exporter object. If close() has not been called, it will automatically finalize
   * any unfinished drawing commands and write the SVG end tag.
   */
  ~SVGExporter();

  /**
   * Returns the canvas for exporting if the SVGExporter is not closed; otherwise, returns nullptr.
   */
  Canvas* getCanvas() const;

  /**
   * Closes the SVG exporter,finalizing any unfinished drawing commands and writing the SVG end tag.
   */
  void close();

 private:
  /**
   * Construct a SVG exporter object
   */
  SVGExporter(std::stringstream& svgStream, Context* context, const Rect& viewBox,
              SVGExportingOptions options);

  bool closed = false;
  SVGExportingContext* drawContext = nullptr;
  Canvas* canvas = nullptr;
};

}  // namespace tgfx