/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <string>
#include "tgfx/core/FontMetrics.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
/**
 * CustomTypefaceBuilder is the base class for creating custom typefaces.
 */
class CustomTypefaceBuilder {
 public:
  virtual ~CustomTypefaceBuilder() = default;

  /**
   * Sets the font name and style for the typeface.
   */
  void setFontName(const std::string& fontFamily, const std::string& fontStyle);

  /**
   * Sets the font metrics for the typeface.
   */
  void setMetrics(const FontMetrics& metrics);

  /**
   * Detaches the typeface being built. After this call, the builder remains valid and can be used
   * to add more glyphs, but the returned typeface is no longer linked to this builder. Any later
   * detached typeface will include glyphs from previous detachments. You can safely release the
   * previously detached typeface and use the new one for rendering. All glyphs added to the same
   * typeface builder share internal caches during rendering.
   */
  virtual std::shared_ptr<Typeface> detach() const = 0;

 protected:
  void updateMetricsBounds(const Rect& bounds, bool firstTime);

  std::string _fontFamily;
  std::string _fontStyle;
  FontMetrics _fontMetrics = {};
  uint32_t uniqueID = 0;
};
}  // namespace tgfx
