/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GlyphRun.h"
#include "tgfx/core/PathEffect.h"

namespace tgfx {
GlyphRun::GlyphRun(Font font, std::vector<GlyphID> glyphIDs, std::vector<Point> positions)
    : _font(std::move(font)), _glyphIDs(std::move(glyphIDs)), _positions(std::move(positions)) {
}

Rect GlyphRun::getBounds(float scale, const Stroke* stroke) const {
  if (scale <= 0.0f) {
    return Rect::MakeEmpty();
  }
  auto font = scale == 1.0f ? _font : _font.makeWithSize(_font.getSize() * scale);
  auto totalBounds = Rect::MakeEmpty();
  size_t index = 0;
  for (auto& glyphID : _glyphIDs) {
    auto bounds = font.getBounds(glyphID);
    auto& position = _positions[index];
    bounds.offset(position.x * scale, position.y * scale);
    totalBounds.join(bounds);
    index++;
  }
  if (!totalBounds.isEmpty() && stroke && !font.getTypeface()->hasColor()) {
    auto strokeWidth = stroke->width * scale;
    totalBounds.outset(strokeWidth, strokeWidth);
  }
  return totalBounds;
}

bool GlyphRun::getPath(Path* path, float scale, const Stroke* stroke) const {
  if (scale <= 0.0f || hasColor()) {
    return {};
  }
  auto font = scale == 1.0f ? _font : _font.makeWithSize(_font.getSize() * scale);
  Path totalPath = {};
  auto glyphCount = _glyphIDs.size();
  for (size_t i = 0; i < glyphCount; ++i) {
    auto& glyphID = _glyphIDs[i];
    Path glyphPath = {};
    if (font.getPath(glyphID, &glyphPath)) {
      auto& position = _positions[i];
      glyphPath.transform(Matrix::MakeTrans(position.x * scale, position.y * scale));
      totalPath.addPath(glyphPath);
    } else {
      return false;
    }
  }
  if (stroke) {
    auto scaledStroke = *stroke;
    scaledStroke.width *= scale;
    auto pathEffect = PathEffect::MakeStroke(&scaledStroke);
    if (pathEffect) {
      pathEffect->applyTo(&totalPath);
    }
  }
  *path = totalPath;
  return true;
}
}  // namespace tgfx
