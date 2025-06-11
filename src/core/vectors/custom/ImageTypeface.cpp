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

#include "ImageTypeface.h"
#include "ImageScalerContext.h"

namespace tgfx {
std::shared_ptr<ImageTypeface> ImageTypeface::Make(uint32_t builderID,
                                                   const std::string& fontFamily,
                                                   const std::string& fontStyle,
                                                   const FontMetrics& metrics,
                                                   const ImageRecordType& glyphRecords) {
  auto typeface = std::shared_ptr<ImageTypeface>(
      new ImageTypeface(builderID, fontFamily, fontStyle, metrics, glyphRecords));
  typeface->weakThis = typeface;
  return typeface;
}

ImageTypeface::ImageTypeface(uint32_t builderID, const std::string& fontFamily,
                             const std::string& fontStyle, const FontMetrics& metrics,
                             const ImageRecordType& glyphRecords)
    : _builderID(builderID), _fontFamily(fontFamily), _fontStyle(fontStyle), _fontMetrics(metrics),
      glyphRecords(glyphRecords) {
}

size_t ImageTypeface::glyphsCount() const {
  return glyphRecords.size();
}

int ImageTypeface::unitsPerEm() const {
  return 128;
}

bool ImageTypeface::hasColor() const {
  return true;
}

bool ImageTypeface::hasOutlines() const {
  return false;
}

std::shared_ptr<Data> ImageTypeface::getBytes() const {
  return nullptr;  // ImageTypeface does not have byte data.
}

std::shared_ptr<Data> ImageTypeface::copyTableData(FontTableTag) const {
  // ImageTypeface does not support table data.
  return nullptr;
}

std::shared_ptr<ScalerContext> ImageTypeface::onCreateScalerContext(float size) const {
  return std::make_shared<ImageScalerContext>(weakThis.lock(), size);
}

std::shared_ptr<ImageTypefaceBuilder::GlyphRecord> ImageTypeface::getGlyphRecord(
    GlyphID glyphID) const {
  if (glyphID == 0 || static_cast<size_t>(glyphID) > glyphRecords.size()) {
    return nullptr;  // Invalid GlyphID
  }
  return glyphRecords[glyphID - 1];  // GlyphID starts from 1
}
}  // namespace tgfx
