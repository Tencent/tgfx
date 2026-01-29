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
      : UserScalerContext(std::move(typeface), size), fauxBoldScale(FauxBoldScale(size)) {
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
      auto fauxBoldSize = textScale * fauxBoldScale;
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
        Stroke stroke(textScale * fauxBoldScale);
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
    shape = Shape::ApplyStroke(std::move(shape), stroke);
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

  float fauxBoldScale = 1.0f;
};

std::shared_ptr<UserTypeface> PathUserTypeface::Make(uint32_t builderID,
                                                     const std::string& fontFamily,
                                                     const std::string& fontStyle,
                                                     const FontMetrics& fontMetrics,
                                                     const Rect& fontBounds, int unitsPerEm,
                                                     const VectorProviderType& glyphPathProviders) {
  auto typeface = std::shared_ptr<PathUserTypeface>(new PathUserTypeface(
      builderID, fontFamily, fontStyle, fontMetrics, fontBounds, unitsPerEm, glyphPathProviders));
  typeface->weakThis = typeface;
  return typeface;
}

PathUserTypeface::PathUserTypeface(uint32_t builderID, const std::string& fontFamily,
                                   const std::string& fontStyle, const FontMetrics& fontMetrics,
                                   const Rect& fontBounds, int unitsPerEm,
                                   const VectorProviderType& glyphPathProviders)
    : UserTypeface(builderID, fontFamily, fontStyle, fontMetrics, fontBounds, unitsPerEm),
      glyphPathProviders(glyphPathProviders) {
}

size_t PathUserTypeface::glyphsCount() const {
  return glyphPathProviders.size();
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
  if (glyphID == 0 || static_cast<size_t>(glyphID) > glyphPathProviders.size()) {
    return nullptr;
  }
  return glyphPathProviders[glyphID - 1];
}
}  // namespace tgfx
