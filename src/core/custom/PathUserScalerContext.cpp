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

#include "PathUserScalerContext.h"
#include "core/PathRasterizer.h"
#include "core/utils/ApplyStrokeToBound.h"
#include "core/utils/FauxBoldScale.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
static Matrix GetTransform(bool fauxItalic, float textSize) {
  auto matrix = Matrix::MakeScale(textSize);
  if (fauxItalic) {
    matrix.postSkew(ITALIC_SKEW, 0.f);
  }
  return matrix;
}

PathUserScalerContext::PathUserScalerContext(std::shared_ptr<Typeface> typeface, float size)
    : UserScalerContext(std::move(typeface), size) {
  fauxBoldScale = FauxBoldScale(textSize);
}

Rect PathUserScalerContext::getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
  auto pathProvider = pathTypeFace()->getGlyphRecord(glyphID);
  if (pathProvider == nullptr) {
    return {};
  }
  auto matrix = GetTransform(fauxItalic, textSize);
  auto path = pathProvider->getPath();
  path.transform(matrix);
  auto bounds = path.getBounds();
  if (bounds.isEmpty()) {
    return {};
  }
  if (fauxBold) {
    auto fauxBoldSize = textSize * fauxBoldScale;
    bounds.outset(fauxBoldSize, fauxBoldSize);
  }
  bounds.roundOut();
  return bounds;
}

bool PathUserScalerContext::generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                         Path* path) const {
  if (path == nullptr) {
    return false;
  }
  auto pathProvider = pathTypeFace()->getGlyphRecord(glyphID);
  if (pathProvider == nullptr) {
    return false;
  }

  *path = pathProvider->getPath();
  auto transform = GetTransform(fauxItalic, textSize);
  path->transform(transform);

  if (fauxBold) {
    auto strokePath = *path;
    Stroke stroke(textSize * fauxBoldScale);
    stroke.applyToPath(&strokePath);
    path->addPath(strokePath, PathOp::Union);
  }
  return true;
}

Rect PathUserScalerContext::getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                              Matrix* matrix) const {
  auto record = pathTypeFace()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return {};
  }
  auto bounds = getBounds(glyphID, fauxBold, false);
  if (stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, &bounds, true);
  }
  if (matrix) {
    matrix->setTranslate(bounds.x(), bounds.y());
  }
  return bounds;
}

bool PathUserScalerContext::readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                       const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr || dstInfo.colorType() != ColorType::ALPHA_8) {
    return false;
  }
  auto pathProvider = pathTypeFace()->getGlyphRecord(glyphID);
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
  auto matrix = Matrix::MakeScale(textSize);
  matrix.postTranslate(-bounds.x(), -bounds.y());
  auto shape = Shape::MakeFrom(pathProvider->getPath());
  shape = Shape::ApplyStroke(std::move(shape), stroke);
  shape = Shape::ApplyMatrix(std::move(shape), matrix);
  auto rasterizer = PathRasterizer::Make(width, height, std::move(shape), true, true);
  if (rasterizer == nullptr) {
    return false;
  }
  rasterizer->readPixels(dstInfo, dstPixels);
  return true;
}

PathUserTypeface* PathUserScalerContext::pathTypeFace() const {
  return static_cast<PathUserTypeface*>(typeface.get());
}
}  // namespace tgfx
