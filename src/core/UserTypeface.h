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

#include <cmath>
#include "core/utils/UniqueID.h"
#include "tgfx/core/FontMetrics.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
class UserTypeface : public Typeface {
 public:
  uint32_t builderID() const {
    return _builderID;
  }

  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override {
    return _fontFamily;
  }

  std::string fontStyle() const override {
    return _fontStyle;
  }

  int unitsPerEm() const override {
    return static_cast<int>(std::ceil(_unitsPerEm));
  }

  float unitsPerEmF() const {
    return _unitsPerEm;
  }

  const FontMetrics& fontMetrics() const {
    return _fontMetrics;
  }

  GlyphID getGlyphID(Unichar) const override {
    return 0;
  }

  std::unique_ptr<Stream> openStream() const override {
    // UserTypeface does not have byte data.
    return nullptr;
  }

  std::shared_ptr<Data> copyTableData(FontTableTag) const override {
    // UserTypeface does not support font tables.
    return nullptr;
  }

  bool isCustom() const override {
    return true;
  }

  bool onComputeBounds(Rect* bounds) const override {
    float invUpem = 1.0f / _unitsPerEm;
    bounds->setLTRB(fontBounds.left * invUpem, fontBounds.top * invUpem,
                    fontBounds.right * invUpem, fontBounds.bottom * invUpem);
    return true;
  }

 protected:
  explicit UserTypeface(uint32_t builderID, const std::string& fontFamily,
                        const std::string& fontStyle, const FontMetrics& fontMetrics,
                        const Rect& fontBounds, float unitsPerEm)
      : _builderID(builderID), _fontFamily(fontFamily), _fontStyle(fontStyle),
        _fontMetrics(fontMetrics), fontBounds(fontBounds), _unitsPerEm(unitsPerEm) {
  }

 private:
  uint32_t _builderID = 0;  // Builder ID for tracking the source builder
  uint32_t _uniqueID = UniqueID::Next();
  std::string _fontFamily;
  std::string _fontStyle;
  FontMetrics _fontMetrics = {};
  Rect fontBounds = {};
  float _unitsPerEm = 1.0f;
};
}  // namespace tgfx
