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

#include "core/GlyphDrawer.h"
#include "core/ScalerContext.h"

namespace tgfx {
GlyphDrawer::GlyphDrawer(float resolutionScale, bool antiAlias, bool needsGammaCorrection)
    : resolutionScale(resolutionScale), antiAlias(antiAlias),
      needsGammaCorrection(needsGammaCorrection) {
}

static float LinearToSRGB(float linear) {
  // The magic numbers are derived from the sRGB specification.
  // See http://www.color.org/chardata/rgb/srgb.xalter.
  if (linear <= 0.0031308f) {
    return linear * 12.92f;
  }
  return 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
}

const std::array<uint8_t, 256>& GlyphDrawer::GammaTable() {
  static const std::array<uint8_t, 256> table = [] {
    std::array<uint8_t, 256> table{};
    table[0] = 0;
    table[255] = 255;
    for (size_t i = 1; i < 255; ++i) {
      auto v = std::roundf(LinearToSRGB(static_cast<float>(i) / 255.f) * 255.f);
      table[i] = static_cast<uint8_t>(v);
    }
    return table;
  }();
  return table;
}

bool GlyphDrawer::fillGlyph(const GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                            const ImageInfo& dstInfo, void* dstPixels) {
  if (glyphFace == nullptr || dstPixels == nullptr || glyphID == 0) {
    return false;
  }
  auto bounds = GetGlyphBounds(glyphFace, glyphID, resolutionScale, stroke);
  if (bounds.isEmpty()) {
    return false;
  }
  if (onFillGlyph(glyphFace, glyphID, stroke, bounds, dstInfo, dstPixels)) {
    return true;
  }
  Path glyphPath = {};
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
  return onFillPath(glyphPath, dstInfo, dstPixels);
}

bool GlyphDrawer::fillPath(const Path& path, const ImageInfo& dstInfo, void* dstPixels) {
  if (dstPixels == nullptr || path.isEmpty()) {
    return false;
  }
  return onFillPath(path, dstInfo, dstPixels);
}

Rect GlyphDrawer::GetGlyphBounds(const GlyphFace* glyphFace, GlyphID glyphID, float resolutionScale,
                                 const Stroke* stroke) {
  Rect bounds = {};
  if (glyphFace == nullptr) {
    return bounds;
  }
  Font font;
  if (glyphFace->asFont(&font)) {
    if (font.scalerContext->canUseImage(font.isFauxBold(), stroke)) {
      bounds = font.scalerContext->getImageTransform(glyphID, nullptr);
    } else {
      bounds = font.getBounds(glyphID);
    }
  } else {
    auto imageCodec = glyphFace->getImage(glyphID, nullptr, nullptr);
    bounds = Rect::MakeWH(imageCodec->width(), imageCodec->height());
  }
  if (bounds.isEmpty()) {
    return bounds;
  }
  if (!glyphFace->hasColor() && stroke != nullptr) {
    bounds.scale(1.f / resolutionScale, 1.f / resolutionScale);
    stroke->applyToBounds(&bounds);
    bounds.scale(resolutionScale, resolutionScale);
  }
  return bounds;
}
bool GlyphDrawer::onFillGlyph(const GlyphFace*, GlyphID, const Stroke*, const Rect&,
                              const ImageInfo&, void*) {
  return false;
}

}  //namespace tgfx