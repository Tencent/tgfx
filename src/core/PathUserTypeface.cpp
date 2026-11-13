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

#include "PathUserTypeface.h"
#include "UserScalerContext.h"
#include "core/PathRasterizer.h"
#include "core/utils/FauxBoldScale.h"
#include "core/utils/MathExtra.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
static Matrix GetTransform(bool fauxItalic, float textScale) {
  auto matrix = Matrix::MakeScale(textScale);
  if (fauxItalic) {
    matrix.postSkew(ITALIC_SKEW, 0.f);
  }
  return matrix;
}

class PathUserScalerContext final : public UserScalerContext {
 public:
  PathUserScalerContext(std::shared_ptr<Typeface> typeface, float size)
      : UserScalerContext(std::move(typeface), size), fauxBoldSize(size * FauxBoldScale(size)) {
  }

  float getAdvance(GlyphID glyphID, bool) const override {
    return pathTypeface()->getGlyphAdvance(glyphID) * textScale;
  }

  Rect getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const override {
    auto pathProvider = pathTypeface()->getPathProvider(glyphID);
    if (pathProvider == nullptr) {
      return {};
    }
    Rect bounds = pathProvider->getBounds();
    if (bounds.isEmpty()) {
      return {};
    }
    auto matrix = GetTransform(fauxItalic, textScale);
    bounds = matrix.mapRect(bounds);
    if (fauxBold) {
      bounds.outset(fauxBoldSize, fauxBoldSize);
    }
    bounds.roundOut();
    return bounds;
  }

  bool generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic, Path* path) const override {
    if (path == nullptr) {
      return false;
    }
    auto pathProvider = pathTypeface()->getPathProvider(glyphID);
    if (pathProvider == nullptr) {
      path->reset();
      return false;
    }
    *path = pathProvider->getPath();
    if (!path->isEmpty()) {
      auto transform = GetTransform(fauxItalic, textScale);
      path->transform(transform);
      if (fauxBold) {
        auto strokePath = *path;
        Stroke stroke(fauxBoldSize);
        stroke.applyToPath(&strokePath);
        path->addPath(strokePath, PathOp::Union);
      }
    }
    return true;
  }

  Rect getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                         Matrix* matrix) const override {
    if (pathTypeface()->getPathProvider(glyphID) == nullptr) {
      return {};
    }
    auto bounds = getBounds(glyphID, fauxBold, false);
    if (bounds.isEmpty()) {
      return {};
    }
    if (stroke != nullptr) {
      ApplyStrokeToBounds(*stroke, &bounds);
    }
    if (matrix) {
      matrix->setTranslate(bounds.x(), bounds.y());
    }
    return bounds;
  }

  bool readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke, const ImageInfo& dstInfo,
                  void* dstPixels, const Point&) const override {
    if (dstInfo.isEmpty() || dstPixels == nullptr || dstInfo.colorType() != ColorType::ALPHA_8) {
      return false;
    }
    auto pathProvider = pathTypeface()->getPathProvider(glyphID);
    if (pathProvider == nullptr) {
      return false;
    }
    auto bounds = getImageTransform(glyphID, fauxBold, stroke, nullptr);
    bounds.roundOut();
    auto width = static_cast<int>(bounds.width());
    auto height = static_cast<int>(bounds.height());
    if (width <= 0 || height <= 0) {
      return false;
    }
    auto matrix = Matrix::MakeScale(textScale);
    matrix.postTranslate(-bounds.x(), -bounds.y());
    auto shape = Shape::MakeFrom(pathProvider);
    if (fauxBold) {
      auto boldStroke = std::make_unique<Stroke>(fauxBoldSize / textScale);
      shape = Shape::ApplyStroke(shape, boldStroke.get());
      shape = Shape::Merge(Shape::MakeFrom(pathProvider), std::move(shape));
    }
    // The stroke parameter is already scaled by matrix.maxScale in the caller (GetScaledStroke).
    // Since ApplyStroke runs before the upcoming Matrix::MakeScale(textScale) transform, we must
    // pre-shrink the stroke width so the downstream matrix re-applies the correct visual width.
    //
    // NOTE: dividing by textScale is exact only when textScale equals the caller's maxScale, which
    // holds when unitsPerEm == originalFontSize. Custom typefaces created with unitsPerEm !=
    // originalFontSize (e.g. unitsPerEm=1024 with fontSize=16) will have textScale != maxScale and
    // this compensation will be off; revisit if such usage is added.
    std::unique_ptr<Stroke> adjustedStroke;
    const Stroke* strokeToApply = stroke;
    if (stroke != nullptr && !FloatNearlyEqual(textScale, 1.0f)) {
      adjustedStroke = std::make_unique<Stroke>(*stroke);
      adjustedStroke->width /= textScale;
      strokeToApply = adjustedStroke.get();
    }
    shape = Shape::ApplyStroke(std::move(shape), strokeToApply);
    shape = Shape::ApplyMatrix(std::move(shape), matrix);
    auto rasterizer = PathRasterizer::MakeFrom(width, height, std::move(shape), true,
#ifdef TGFX_USE_TEXT_GAMMA_CORRECTION
                                               true
#else
                                               false
#endif
    );
    if (rasterizer == nullptr) {
      return false;
    }
    rasterizer->readPixels(dstInfo, dstPixels);
    return true;
  }

 private:
  PathUserTypeface* pathTypeface() const {
    return static_cast<PathUserTypeface*>(userTypeface());
  }

  float fauxBoldSize = 0.0f;
};

std::shared_ptr<UserTypeface> PathUserTypeface::Make(uint32_t builderID,
                                                     const std::string& fontFamily,
                                                     const std::string& fontStyle,
                                                     const FontMetrics& fontMetrics,
                                                     const Rect& fontBounds, int unitsPerEm,
                                                     const GlyphRecords& glyphRecords) {
  auto typeface = std::shared_ptr<PathUserTypeface>(new PathUserTypeface(
      builderID, fontFamily, fontStyle, fontMetrics, fontBounds, unitsPerEm, glyphRecords));
  typeface->weakThis = typeface;
  return typeface;
}

PathUserTypeface::PathUserTypeface(uint32_t builderID, const std::string& fontFamily,
                                   const std::string& fontStyle, const FontMetrics& fontMetrics,
                                   const Rect& fontBounds, int unitsPerEm,
                                   const GlyphRecords& glyphRecords)
    : UserTypeface(builderID, fontFamily, fontStyle, fontMetrics, fontBounds, unitsPerEm),
      glyphRecords(glyphRecords) {
}

size_t PathUserTypeface::glyphsCount() const {
  return glyphRecords.size();
}

bool PathUserTypeface::hasColor() const {
  return false;
}

bool PathUserTypeface::hasOutlines() const {
  return true;
}

std::shared_ptr<ScalerContext> PathUserTypeface::onCreateScalerContext(float size) const {
  return std::make_shared<PathUserScalerContext>(weakThis.lock(), size);
}

std::shared_ptr<PathProvider> PathUserTypeface::getPathProvider(GlyphID glyphID) const {
  if (glyphID == 0 || static_cast<size_t>(glyphID) > glyphRecords.size()) {
    return nullptr;
  }
  return glyphRecords[glyphID - 1].pathProvider;
}

float PathUserTypeface::getGlyphAdvance(GlyphID glyphID) const {
  if (glyphID == 0 || static_cast<size_t>(glyphID) > glyphRecords.size()) {
    return 0.0f;
  }
  return glyphRecords[glyphID - 1].advance;
}
}  // namespace tgfx
