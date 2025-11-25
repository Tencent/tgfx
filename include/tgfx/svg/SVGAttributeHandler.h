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

#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

/**
 * Abstract callback interface for SVG parsing.
 * Pass an instance to SVGDOM::Make to customize attribute handling.
 */
class SVGParseSetter {
 public:
  virtual ~SVGParseSetter() = default;

  /**
   * Called when setting attributes on an SVGNode during parsing.
   * @return true to allow the attribute to be set, false to skip it.
   * Note: Essential rendering attributes (e.g., fill) are always set regardless of the return value.
   */
  virtual bool setAttribute(SVGNode& node, const std::string& name, const std::string& value) = 0;
};

/**
 * Abstract callback interface for exporting SVG filters.
 * Pass an instance to SVGExporter to customize exporting filters.
 */
class SVGExportWriter {
 public:
  virtual ~SVGExportWriter() = default;

  /**
   * Called when exporting a BlurImageFilter.
   * @return A DOMAttribute to be added to the <filter> element as a custom attribute.
   */
  virtual DOMAttribute writeBlurImageFilter(float blurrinessX, float blurrinessY,
                                            TileMode tileMode) = 0;

  /**
   * Called when exporting a DropShadowImageFilter.
   * @return A DOMAttribute to be added to the <filter> element as a custom attribute.
   */
  virtual DOMAttribute writeDropShadowImageFilter(float dx, float dy, float blurrinessX,
                                                  float blurrinessY, Color color,
                                                  bool dropShadowOnly) = 0;

  /**
   * Called when exporting an InnerShadowImageFilter.
   * @return A DOMAttribute to be added to the <filter> element as a custom attribute.
   */
  virtual DOMAttribute writeInnerShadowImageFilter(float dx, float dy, float blurrinessX,
                                                   float blurrinessY, Color color,
                                                   bool innerShadowOnly) = 0;
};

}  // namespace tgfx