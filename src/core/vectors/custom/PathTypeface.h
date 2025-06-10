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

#include "core/utils/UniqueID.h"
#include "tgfx/core/FontMetrics.h"
#include "tgfx/core/PathTypefaceBuilder.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
class PathTypeface final : public Typeface {
 public:
  using VectorRecordType = std::vector<std::shared_ptr<PathTypefaceBuilder::GlyphRecord>>;

  static std::shared_ptr<PathTypeface> Make(uint32_t builderID, const std::string& fontFamily,
                                            const std::string& fontStyle,
                                            const FontMetrics& metrics,
                                            const VectorRecordType& glyphRecords);

  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  std::string fontFamily() const override {
    return _fontFamily;
  }

  std::string fontStyle() const override {
    return _fontStyle;
  }

  const FontMetrics& fontMetrics() const {
    return _fontMetrics;
  }

  size_t glyphsCount() const override;

  int unitsPerEm() const override;

  bool hasColor() const override;

  bool hasOutlines() const override;

  GlyphID getGlyphID(Unichar unichar) const override;

  std::shared_ptr<Data> getBytes() const override;

  std::shared_ptr<Data> copyTableData(FontTableTag) const override;

  std::shared_ptr<PathTypefaceBuilder::GlyphRecord> getGlyphRecord(GlyphID glyphID) const;

 private:
  explicit PathTypeface(uint32_t builderID, const std::string& fontFamily,
                        const std::string& fontStyle, const FontMetrics& metrics,
                        const VectorRecordType& glyphRecords);

  void initCharGlyphIDMap();

  uint32_t getActiveID() const override {
    return _builderID;
  }

  std::shared_ptr<ScalerContext> onCreateScalerContext(float size) const override;

  uint32_t _builderID = 0;
  uint32_t _uniqueID = UniqueID::Next();
  std::string _fontFamily;
  std::string _fontStyle;
  FontMetrics _fontMetrics;
  VectorRecordType glyphRecords;
  std::unordered_map<Unichar, GlyphID> unicharToGlyphIDMap;
};
}  // namespace tgfx
