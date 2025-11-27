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

#include "tgfx/core/Color.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

/**
 * Abstract callback interface for SVG filter export operations.
 * Implementations can be passed to SVGExporter.
 */
class SVGExportWriter {
 public:
  virtual ~SVGExportWriter() = default;

  /**
   * Called when exporting a BlurImageFilter to SVG.
   * return A DOMAttribute to be added to the <filter> element as a custom attribute.
   */
  virtual DOMAttribute writeBlurImageFilter(float blurrinessX, float blurrinessY,
                                            TileMode tileMode) = 0;

  /**
   * Called when exporting a DropShadowImageFilter to SVG.
   * return A DOMAttribute to be added to the <filter> element as a custom attribute.
   */
  virtual DOMAttribute writeDropShadowImageFilter(float dx, float dy, float blurrinessX,
                                                  float blurrinessY, Color color,
                                                  bool dropShadowOnly) = 0;

  /**
   * Called when exporting an InnerShadowImageFilter to SVG.
   * return A DOMAttribute to be added to the <filter> element as a custom attribute.
   */
  virtual DOMAttribute writeInnerShadowImageFilter(float dx, float dy, float blurrinessX,
                                                   float blurrinessY, Color color,
                                                   bool innerShadowOnly) = 0;
};

}  // namespace tgfx