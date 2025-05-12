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

#include "GlyphImageCodec.h"
#include "core/GlyphDrawer.h"
#include "core/ScalerContext.h"
#include "png.h"

namespace tgfx {
std::shared_ptr<GlyphImageCodec> GlyphImageCodec::MakeFrom(std::shared_ptr<GlyphFace> glyphFace,
                                                           GlyphID glyphID, const Stroke* stroke,
                                                           float resolutionScale) {
  if (glyphFace == nullptr) {
    return nullptr;
  }

  Rect glyphBounds;
  if (glyphFace->hasColor()) {
    Font font;
    if (glyphFace->asFont(&font)) {
      glyphBounds = font.scalerContext->getImageTransform({glyphID}, nullptr);
    } else {
      auto imageCodec = glyphFace->getImage(glyphID, nullptr, nullptr);
      glyphBounds = Rect::MakeWH(imageCodec->width(), imageCodec->height());
    }
  } else {
    glyphBounds = glyphFace->getBounds(glyphID);
    if (stroke != nullptr) {
      glyphBounds.scale(1.f / resolutionScale, 1.f / resolutionScale);
      stroke->applyToBounds(&glyphBounds);
      glyphBounds.scale(resolutionScale, resolutionScale);
    }
  }
  if (glyphBounds.isEmpty()) {
    return nullptr;
  }

  return std::shared_ptr<GlyphImageCodec>(
      new GlyphImageCodec(std::move(glyphFace), glyphID, glyphBounds, stroke, resolutionScale));
}

GlyphImageCodec::GlyphImageCodec(std::shared_ptr<GlyphFace> glyphFace, GlyphID glyphID,
                                 const Rect& bounds, const Stroke* s, float resolutionScale)
    : ImageCodec(static_cast<int>(std::ceilf(bounds.width())),
                 static_cast<int>(std::ceilf(bounds.height())), Orientation::LeftTop),
      glyphFace(std::move(glyphFace)), glyphID(glyphID), resolutionScale(resolutionScale),
      bounds(bounds) {
  if (s != nullptr) {
    stroke = new Stroke(*s);
  }
  calculateMatrix();
}

void GlyphImageCodec::calculateMatrix() {
  bool useImage = glyphFace->hasColor();
  if (!useImage) {
    Font font;
    if (glyphFace->asFont(&font)) {
      GlyphStyle glyphStyle{glyphID, font.isFauxBold(), font.isFauxItalic(), stroke};
      useImage = font.scalerContext->canUseImage(glyphStyle);
    }
  }
  if (useImage) {
    imageCodec = glyphFace->getImage(glyphID, stroke, &matrix);
  }
  if (imageCodec == nullptr) {
    matrix = Matrix::MakeTrans(bounds.x(), bounds.y());
  }
}

GlyphImageCodec::~GlyphImageCodec() {
  delete stroke;
}

bool GlyphImageCodec::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (imageCodec != nullptr) {
    return imageCodec->readPixels(dstInfo, dstPixels);
  }

  Path glyphPath;
  if (!glyphFace->getPath(glyphID, &glyphPath)) {
    return false;
  }

  if (glyphPath.isEmpty()) {
    return false;
  }

  if (stroke != nullptr) {
    auto scale = 1.0f / resolutionScale;
    glyphPath.transform(Matrix::MakeScale(scale, scale));
    stroke->applyToPath(&glyphPath);
    scale = resolutionScale;
    glyphPath.transform(Matrix::MakeScale(scale, scale));
  }

  glyphPath.transform(Matrix::MakeTrans(-bounds.x(), -bounds.y()));

  auto glyphDrawer = GlyphDrawer::Make({}, true, true);
  if (glyphDrawer == nullptr) {
    return false;
  }

  return glyphDrawer->fillPath(glyphPath, {}, dstInfo, dstPixels);
}
}  // namespace tgfx
