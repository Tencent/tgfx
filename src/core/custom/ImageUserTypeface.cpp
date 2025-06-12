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

#include "ImageUserTypeface.h"
#include "ImageUserScalerContext.h"

namespace tgfx {
std::shared_ptr<UserTypeface> ImageUserTypeface::Make(uint32_t builderID,
                                                      const std::string& fontFamily,
                                                      const std::string& fontStyle,
                                                      const FontMetrics& metrics,
                                                      const ImageRecordType& glyphRecords) {
  auto typeface = std::shared_ptr<ImageUserTypeface>(
      new ImageUserTypeface(builderID, fontFamily, fontStyle, metrics, glyphRecords));
  typeface->weakThis = typeface;
  return typeface;
}

ImageUserTypeface::ImageUserTypeface(uint32_t builderID, const std::string& fontFamily,
                                     const std::string& fontStyle, const FontMetrics& metrics,
                                     const ImageRecordType& glyphRecords)
    : UserTypeface(builderID, fontFamily, fontStyle, metrics), glyphRecords(glyphRecords) {
}

size_t ImageUserTypeface::glyphsCount() const {
  return glyphRecords.size();
}

int ImageUserTypeface::unitsPerEm() const {
  return 128;
}

bool ImageUserTypeface::hasColor() const {
  return true;
}

bool ImageUserTypeface::hasOutlines() const {
  return false;
}

std::shared_ptr<ScalerContext> ImageUserTypeface::onCreateScalerContext(float size) const {
  return std::make_shared<ImageUserScalerContext>(weakThis.lock(), size);
}

std::shared_ptr<ImageTypefaceBuilder::GlyphRecord> ImageUserTypeface::getGlyphRecord(
    GlyphID glyphID) const {
  if (glyphID == 0 || static_cast<size_t>(glyphID) > glyphRecords.size()) {
    return nullptr;  // Invalid GlyphID
  }
  return glyphRecords[glyphID - 1];  // GlyphID starts from 1
}
}  // namespace tgfx
