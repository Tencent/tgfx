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
#include "core/utils/UniqueID.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
class VirtualTypeface final : public Typeface {
 public:
  explicit VirtualTypeface(bool hasColor);

  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override {
    return "";
  }

  std::string fontStyle() const override {
    return "";
  }

  size_t glyphsCount() const override {
    return UINT16_MAX;
  }

  int unitsPerEm() const override {
    return 0;
  }

  bool hasColor() const override {
    return _hasColor;
  }

  bool hasOutlines() const override {
    return false;
  }

  GlyphID getGlyphID(Unichar /*unichar*/) const override {
    return 0;
  }

  std::shared_ptr<Data> getBytes() const override {
    return nullptr;
  }

  std::shared_ptr<Data> copyTableData(FontTableTag /*tag*/) const override {
    return nullptr;
  }

  std::shared_ptr<ScalerContext> createScalerContext(float size) const override;

 private:
  uint32_t _uniqueID = 0;
  bool _hasColor = false;

  static bool GetPath(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID, bool fauxBold,
                      bool fauxItalic, Path* path);
  static std::shared_ptr<ImageBuffer> GetImage(const std::shared_ptr<Typeface>& typeface,
                                               GlyphID glyphID, bool tryHardware);
  static Size GetImageTransform(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID,
                                Matrix* matrixOut);
  static Rect GetBounds(const std::shared_ptr<Typeface>& typeface, GlyphID glyphID, bool fauxBold,
                        bool fauxItalic);

  friend class VirtualScalerContext;
};
}  // namespace tgfx
