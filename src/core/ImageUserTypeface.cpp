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

#include "ImageUserTypeface.h"
#include "UserScalerContext.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

class ImageUserScalerContext final : public UserScalerContext {
 public:
  ImageUserScalerContext(std::shared_ptr<Typeface> typeface, float size)
      : UserScalerContext(std::move(typeface), size) {
  }

  float getAdvance(GlyphID glyphID, bool) const override {
    return imageTypeface()->getGlyphAdvance(glyphID) * textScale;
  }

  Rect getBounds(GlyphID glyphID, bool, bool fauxItalic) const override {
    auto record = imageTypeface()->getGlyphRecord(glyphID);
    if (record == nullptr || record->image == nullptr) {
      return {};
    }
    auto bounds = Rect::MakeXYWH(record->offset.x, record->offset.y,
                                 static_cast<float>(record->image->width()),
                                 static_cast<float>(record->image->height()));
    auto matrix = Matrix::MakeScale(textScale, textScale);
    if (fauxItalic) {
      matrix.postSkew(ITALIC_SKEW, 0.f);
    }
    bounds = matrix.mapRect(bounds);
    return bounds;
  }

  bool generatePath(GlyphID, bool, bool, Path*) const override {
    return false;
  }

  Rect getImageTransform(GlyphID glyphID, bool, const Stroke*, Matrix* matrix) const override {
    auto record = imageTypeface()->getGlyphRecord(glyphID);
    if (record == nullptr || record->image == nullptr) {
      return {};
    }
    if (matrix) {
      matrix->setScale(textScale, textScale);
      matrix->preTranslate(record->offset.x, record->offset.y);
    }
    return Rect::MakeXYWH(record->offset.x, record->offset.y,
                          static_cast<float>(record->image->width()),
                          static_cast<float>(record->image->height()));
  }

  bool readPixels(GlyphID glyphID, bool, const Stroke*, const ImageInfo& dstInfo, void* dstPixels,
                  const Point&) const override {
    if (dstInfo.isEmpty() || dstPixels == nullptr) {
      return false;
    }
    auto record = imageTypeface()->getGlyphRecord(glyphID);
    if (record == nullptr || record->image == nullptr) {
      return false;
    }
    return record->image->readPixels(dstInfo, dstPixels);
  }

  float getBackingSize() const override {
    return static_cast<float>(userTypeface()->unitsPerEm());
  }

 private:
  ImageUserTypeface* imageTypeface() const {
    return static_cast<ImageUserTypeface*>(userTypeface());
  }
};

//////////////

std::shared_ptr<UserTypeface> ImageUserTypeface::Make(uint32_t builderID,
                                                      const std::string& fontFamily,
                                                      const std::string& fontStyle,
                                                      const FontMetrics& fontMetrics,
                                                      const Rect& fontBounds, int unitsPerEm,
                                                      const GlyphRecords& glyphRecords) {
  auto typeface = std::shared_ptr<ImageUserTypeface>(new ImageUserTypeface(
      builderID, fontFamily, fontStyle, fontMetrics, fontBounds, unitsPerEm, glyphRecords));
  typeface->weakThis = typeface;
  return typeface;
}

ImageUserTypeface::ImageUserTypeface(uint32_t builderID, const std::string& fontFamily,
                                     const std::string& fontStyle, const FontMetrics& fontMetrics,
                                     const Rect& fontBounds, int unitsPerEm,
                                     const GlyphRecords& glyphRecords)
    : UserTypeface(builderID, fontFamily, fontStyle, fontMetrics, fontBounds, unitsPerEm),
      glyphRecords(glyphRecords) {
}

size_t ImageUserTypeface::glyphsCount() const {
  return glyphRecords.size();
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

std::shared_ptr<ImageTypefaceBuilder::ImageGlyphRecord> ImageUserTypeface::getGlyphRecord(
    GlyphID glyphID) const {
  if (glyphID == 0 || static_cast<size_t>(glyphID) > glyphRecords.size()) {
    return nullptr;  // Invalid GlyphID
  }
  return glyphRecords[glyphID - 1];  // GlyphID starts from 1
}

float ImageUserTypeface::getGlyphAdvance(GlyphID glyphID) const {
  auto record = getGlyphRecord(glyphID);
  if (record == nullptr) {
    return 0.0f;
  }
  return record->advance;
}
}  // namespace tgfx
