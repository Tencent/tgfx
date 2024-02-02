/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <mutex>
#include "ft2build.h"
#include FT_FREETYPE_H
#include "FTFontData.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
class FTTypeface : public Typeface {
 public:
  static std::shared_ptr<FTTypeface> Make(FTFontData data);

  ~FTTypeface() override;

  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override;

  std::string fontStyle() const override;

  int glyphsCount() const override;

  int unitsPerEm() const override;

  bool hasColor() const override;

  GlyphID getGlyphID(Unichar unichar) const override;

  std::shared_ptr<Data> getBytes() const override;

  std::shared_ptr<Data> copyTableData(FontTableTag tag) const override;

 protected:
  float getAdvance(GlyphID glyphID, float size, bool fauxBold, bool fauxItalic,
                   bool verticalText) const override;

  FontMetrics getMetrics(float size) const override;

  bool getPath(GlyphID glyphID, float size, bool fauxBold, bool fauxItalic,
               Path* path) const override;

  std::shared_ptr<ImageBuffer> getGlyphImage(GlyphID glyphID, float size, bool fauxItalic,
                                             Matrix* matrix) const override;

  Rect getBounds(GlyphID glyphID, float size, bool fauxBold, bool fauxItalic) const override;

  Point getVerticalOffset(GlyphID glyphID, float size, bool fauxBold,
                          bool fauxItalic) const override;

 private:
  uint32_t _uniqueID = 0;
  FTFontData data;
  mutable std::mutex locker = {};
  FT_Face face = nullptr;
  std::weak_ptr<FTTypeface> weakThis;

  static int GetUnitsPerEm(FT_Face face);

  FTTypeface(FTFontData data, FT_Face face);

  friend class FTScalerContext;
};
}  // namespace tgfx
