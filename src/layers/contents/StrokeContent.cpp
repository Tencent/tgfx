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

#include "StrokeContent.h"
#include "core/utils/Log.h"
#include "core/utils/StrokeUtils.h"
#include "core/utils/Types.h"

namespace tgfx {

StrokeContent::StrokeContent(std::unique_ptr<GeometryContent> content, const Stroke& stroke)
    : content(std::move(content)), _stroke(stroke) {
}

Rect StrokeContent::getBounds() const {
  auto bounds = content->getBounds();
  ApplyStrokeToBounds(_stroke, &bounds, Matrix::I(), content->mayHaveSharpCorners());
  return bounds;
}

bool StrokeContent::hasSameGeometry(const GeometryContent* other) const {
  if (other == nullptr || Types::Get(this) != Types::Get(other)) {
    return false;
  }
  auto otherStroke = static_cast<const StrokeContent*>(other);
  if (!(_stroke == otherStroke->_stroke)) {
    return false;
  }
  return content->hasSameGeometry(otherStroke->content.get());
}

const Color& StrokeContent::getColor() const {
  return content->getColor();
}

const std::shared_ptr<Shader>& StrokeContent::getShader() const {
  return content->getShader();
}

const BlendMode& StrokeContent::getBlendMode() const {
  return content->getBlendMode();
}

Rect StrokeContent::getTightBounds(const Matrix& matrix, const Stroke* stroke) const {
  DEBUG_ASSERT(stroke == nullptr);
  (void)stroke;
  return content->getTightBounds(matrix, &_stroke);
}

bool StrokeContent::hitTestPoint(float localX, float localY, const Stroke* stroke) const {
  DEBUG_ASSERT(stroke == nullptr);
  (void)stroke;
  return content->hitTestPoint(localX, localY, &_stroke);
}

bool StrokeContent::drawDefault(Canvas* canvas, float alpha, bool antiAlias,
                                const Stroke* stroke) const {
  DEBUG_ASSERT(stroke == nullptr);
  (void)stroke;
  return content->drawDefault(canvas, alpha, antiAlias, &_stroke);
}

void StrokeContent::drawForeground(Canvas* canvas, float alpha, bool antiAlias,
                                   const Stroke* stroke) const {
  DEBUG_ASSERT(stroke == nullptr);
  (void)stroke;
  content->drawForeground(canvas, alpha, antiAlias, &_stroke);
}

void StrokeContent::drawContour(Canvas* canvas, bool antiAlias, const Stroke* stroke) const {
  DEBUG_ASSERT(stroke == nullptr);
  (void)stroke;
  content->drawContour(canvas, antiAlias, &_stroke);
}

bool StrokeContent::contourEqualsOpaqueContent() const {
  return content->contourEqualsOpaqueContent();
}

bool StrokeContent::hasBlendMode() const {
  return content->hasBlendMode();
}

}  // namespace tgfx
