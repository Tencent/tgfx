/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/svg/SVGAttributeHandler.h"

namespace tgfx {
class SVGExportContext;

/**
 * Defines flags for SVG exporting that influence the readability and functionality of the exported
 * SVG.
 */
class SVGExportFlags {
 public:
  /**
   * Forces text to be converted to paths in the exported SVG. By default, text is exported as is.
   * Note that this only applies to fonts with outlines. Fonts without outlines, such as emoji and
   * web fonts, will still be exported as text.
   */
  static constexpr uint32_t ConvertTextToPaths = 1 << 0;

  /**
   * Disable pretty XML formatting in the exported SVG. By default, spaces ('\t') and 
   * newlines ('\n') are added to the exported SVG text for better readability.
   */
  static constexpr uint32_t DisablePrettyXML = 1 << 1;

  /**
   * Disable warnings for unsupported attributes. By default, warnings are logged to the console
   * when exporting attributes that the SVG standard does not support.
   */
  static constexpr uint32_t DisableWarnings = 1 << 2;
};

/**
 * SVGExporter is used to convert drawing commands from the Canvas into SVG text.
 *  
 * Some features are not supported when exporting to SVG:
 * - Blend modes:
 * Clear, Src, Dst, DstOver, SrcIn, DstIn, SrcOut, DstOut, SrcATop, DstATop, Xor, and Modulate are
 * not supported.
 * 
 * - Image filters:
 * Compose and Runtime are not supported.
 *
 * - Color filters:
 * Compose and AlphaThreshold filters are not supported. The Blend filter is partially supported,
 * similar to blend modes.
 *
 * - Shaders:
 * ColorFilter, Blend, and Matrix are not supported. Gradient shaders are partially supported.
 * 
 * - Gradient shaders:
 * Conic gradients are not supported.
 *
 * - Mask filters:
 * Mask filters are created using shaders. Any unsupported shaders will also result in unsupported
 * mask filters.
 */

class SVGExporter {
 public:
  /**
   * Creates a shared pointer to an SVG exporter object, which can be used to export SVG text.
   *
   * @param svgStream The stream to store the SVG text.
   * @param context The context used to convert some rendering commands into image data.
   * @param viewBox The viewBox of the SVG. Content that exceeds this area will be clipped.
   * @param exportFlags Flags for exporting SVG text.
   * @return A shared pointer to the SVG exporter object. If svgStream is nullptr, context is
   * nullptr, or viewBox is empty, nullptr is returned.
   */
  static std::shared_ptr<SVGExporter> Make(
      const std::shared_ptr<WriteStream>& svgStream, Context* context, const Rect& viewBox,
      uint32_t exportFlags = 0, const std::shared_ptr<SVGExportWriter>& writer = nullptr);

  /**
   * Destroys the SVG exporter object. If close() hasn't been called, it will be invoked 
   * automatically. 
   */
  ~SVGExporter();

  /**
   * Returns the canvas for exporting if the SVGExporter is not closed; otherwise, returns nullptr. 
   */
  Canvas* getCanvas() const;

  /**
   * Closes the SVG exporter, finalizing any unfinished drawing commands and writing the SVG end 
   * tag.
   */
  void close();

 private:
  /**
   * Construct a SVG exporter object
   */
  SVGExporter(const std::shared_ptr<WriteStream>& svgStream, Context* context, const Rect& viewBox,
              uint32_t exportFlags, const std::shared_ptr<SVGExportWriter>& writer);

  SVGExportContext* drawContext = nullptr;
  Canvas* canvas = nullptr;
};

}  // namespace tgfx