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

namespace tgfx {
GlyphDrawer::GlyphDrawer(const Matrix& matrix, bool antiAlias, bool needsGammaCorrection)
    : matrix(matrix), antiAlias(antiAlias), needsGammaCorrection(needsGammaCorrection) {
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

bool GlyphDrawer::fillText(const GlyphRunList* glyphRunList, const Stroke* stroke,
                           const ImageInfo& dstInfo, void* dstPixels) {
  if (glyphRunList->hasColor()) {
    return false;
  }
  if (onFillText(glyphRunList, stroke, matrix, antiAlias)) {
    return true;
  }
  Path path = {};
  if (!glyphRunList->getPath(&path, matrix.getMaxScale())) {
    return false;
  }
  if (stroke) {
    stroke->applyToPath(&path);
  }
  path.transform(matrix);
  onFillPath(path, {}, dstInfo, dstPixels);
  return true;
}

bool GlyphDrawer::fillGlyph(const GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                            const ImageInfo& dstInfo, void* dstPixels) {
  if (glyphFace == nullptr || glyphFace->hasColor() || dstPixels == nullptr) {
    return false;
  }
  if (onFillGlyph(glyphFace, glyphID, stroke, dstInfo, dstPixels)) {
    return true;
  }

  Path path = {};
  if (!glyphFace->getPath(glyphID, &path)) {
    return false;
  }

  if (stroke != nullptr) {
    stroke->applyToPath(&path);
  }
  path.transform(matrix);
  return onFillPath(path, {}, dstInfo, dstPixels);
}

bool GlyphDrawer::fillPath(const Path& path, const Matrix& mat, const ImageInfo& dstInfo,
                           void* dstPixels) {
  if (dstPixels == nullptr || path.isEmpty()) {
    return false;
  }
  return onFillPath(path, mat, dstInfo, dstPixels);
}

}  //namespace tgfx