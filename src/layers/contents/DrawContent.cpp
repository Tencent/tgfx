/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "DrawContent.h"
#include "core/utils/StrokeUtils.h"
#include "core/utils/Types.h"

namespace tgfx {

static bool StrokeEquals(const Stroke* a, const Stroke* b) {
  if (a == b) {
    return true;
  }
  if (a == nullptr || b == nullptr) {
    return false;
  }
  return *a == *b;
}

DrawContent::DrawContent(const LayerPaint& paint) : blendMode(paint.blendMode) {
  auto paintShader = paint.shader;
  if (paintShader) {
    Color shaderColor = {};
    if (paintShader->asColor(&shaderColor)) {
      shaderColor.alpha *= paint.color.alpha;
      color = shaderColor;
    } else {
      color = paint.color;
      shader = std::move(paintShader);
    }
  } else {
    color = paint.color;
  }
  if (paint.style == PaintStyle::Stroke) {
    stroke = std::make_unique<Stroke>(paint.stroke);
  }
}

Rect DrawContent::getBounds() const {
  auto bounds = onGetBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
  }
  return bounds;
}

void DrawContent::drawContour(Canvas* canvas, bool antiAlias) const {
  Paint paint = {};
  paint.setAntiAlias(antiAlias);
  if (stroke) {
    paint.setStyle(PaintStyle::Stroke);
    paint.setStroke(*stroke);
  }
  if (shader && shader->isAImage()) {
    paint.setShader(shader);
  }
  onDraw(canvas, paint);
}

bool DrawContent::contourEqualsOpaqueContent() const {
  if (color.alpha <= 0) {
    return false;
  }
  if (shader && !shader->isAImage()) {
    return shader->isOpaque();
  }
  return true;
}

bool DrawContent::drawDefault(Canvas* canvas, float alpha, bool antiAlias) const {
  if (color.alpha <= 0) {
    return false;
  }
  Paint paint = {};
  paint.setAntiAlias(antiAlias);
  paint.setColor(color);
  paint.setAlpha(color.alpha * alpha);
  if (shader) {
    paint.setShader(shader);
  }
  paint.setBlendMode(blendMode);
  if (stroke) {
    paint.setStyle(PaintStyle::Stroke);
    paint.setStroke(*stroke);
  }
  onDraw(canvas, paint);
  return false;
}

void DrawContent::drawForeground(Canvas*, float, bool) const {
}

const std::shared_ptr<Shader>& DrawContent::getShader() const {
  return shader;
}

bool DrawContent::hasSameGeometry(const GeometryContent* other) const {
  if (other == nullptr || Types::Get(this) != Types::Get(other)) {
    return false;
  }
  auto otherContent = static_cast<const DrawContent*>(other);
  if (!StrokeEquals(stroke.get(), otherContent->stroke.get())) {
    return false;
  }
  return onHasSameGeometry(other);
}

bool DrawContent::hasBlendMode() const {
  return blendMode != BlendMode::SrcOver;
}

}  // namespace tgfx
