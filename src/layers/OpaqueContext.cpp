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

#include "OpaqueContext.h"
#include "core/utils/RectToRectMatrix.h"
#include "core/utils/StrokeUtils.h"
#include "layers/OpaqueBoundsHelper.h"
#include "layers/OpaqueThreshold.h"

namespace tgfx {

class PendingShapeAutoReset {
 public:
  PendingShapeAutoReset(OpaqueContext* context, const OpaqueContext::OpaqueShape& opaqueShape,
                        MCState state, Brush brush)
      : context(context),
        opaqueShape(opaqueShape),
        state(std::move(state)),
        brush(std::move(brush)) {
  }

  ~PendingShapeAutoReset() {
    context->resetPendingShape(opaqueShape, state, brush);
  }

 private:
  OpaqueContext* context = nullptr;
  OpaqueContext::OpaqueShape opaqueShape;
  MCState state;
  Brush brush;
};

OpaqueContext::OpaqueContext() {
  opaqueBounds.reserve(3);
}

void OpaqueContext::drawFill(const Brush& brush) {
  static OpaqueShape FillShape = OpaqueShape(OpaqueShape::Type::Fill);
  drawOpaqueShape(FillShape, {}, brush);
}

void OpaqueContext::drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                             const Stroke* stroke) {
  drawOpaqueShape(OpaqueShape(rect, stroke), state, brush);
}

void OpaqueContext::drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                              const Stroke* stroke) {
  drawOpaqueShape(OpaqueShape(rRect, stroke), state, brush);
}

void OpaqueContext::drawPath(const Path& path, const MCState& state, const Brush& brush) {
  drawOpaqueShape(OpaqueShape(path), state, brush);
}

void OpaqueContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const Brush& brush, const Stroke* stroke) {
  drawOpaqueShape(OpaqueShape(std::move(shape), stroke), state, brush);
}

void OpaqueContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                              const MCState& state, const Brush& brush) {
  auto newBrush = brush;
  newBrush.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
  drawRect(Rect::MakeWH(image->width(), image->height()), state, newBrush, nullptr);
}

void OpaqueContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                  const Rect& dstRect, const SamplingOptions& sampling,
                                  const MCState& state, const Brush& brush,
                                  SrcRectConstraint constraint) {
  if (constraint != SrcRectConstraint::Strict) {
    auto newState = state;
    newState.matrix.preConcat(MakeRectToRectMatrix(srcRect, dstRect));
    auto path = Path();
    path.addRect(dstRect);
    path.transform(state.matrix);
    newState.clip.addPath(path);
    auto newBrush = brush;
    newBrush.shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp, sampling);
    drawRect(dstRect, newState, newBrush, nullptr);
    return;
  }
  auto bounds = state.matrix.mapRect(dstRect);
  if (containOpaqueBound(bounds)) {
    return;
  }
  pictureContext.drawImageRect(image, srcRect, dstRect, sampling, state, brush, constraint);
}

void OpaqueContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const MCState& state, const Brush& brush,
                                     const Stroke* stroke) {
  auto bounds = glyphRunList->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
  }
  bounds = state.matrix.mapRect(bounds);
  if (containOpaqueBound(bounds)) {
    return;
  }
  pictureContext.drawGlyphRunList(glyphRunList, state, brush, stroke);
}

void OpaqueContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  picture->playback(this, state);
}

void OpaqueContext::drawLayer(std::shared_ptr<Picture> picture,
                              std::shared_ptr<ImageFilter> filter, const MCState& state,
                              const Brush& brush) {
  if (brush.nothingToDraw()) {
    return;
  }
  if (!filter && !brush.maskFilter) {
    drawPicture(picture, state);
    return;
  }
  if (!picture->hasUnboundedFill()) {
    auto bounds = picture->getBounds();
    if (filter) {
      bounds = filter->filterBounds(bounds);
    }
    bounds = state.matrix.mapRect(bounds);
    if (containOpaqueBound(bounds)) {
      return;
    }
  }
  flushPendingShape();
  pictureContext.drawLayer(picture, filter, state, brush);
}

void OpaqueContext::drawOpaqueShape(const OpaqueShape& opaqueShape, const MCState& state,
                                    const Brush& brush) {
  if (canAppend(opaqueShape, state, brush)) {
    appendFill(brush);
    return;
  }
  flushPendingShape(opaqueShape, state, brush);
}

bool OpaqueContext::containOpaqueBound(const Rect& bounds) {
  return OpaqueBoundsHelper::Contains(opaqueBounds, bounds);
}

void OpaqueContext::mergeOpaqueBound(const Rect& bounds) {
  OpaqueBoundsHelper::Merge(opaqueBounds, bounds);
}

std::shared_ptr<Picture> OpaqueContext::finishRecordingAsPicture() {
  flushPendingShape();
  return pictureContext.finishRecordingAsPicture();
}

bool OpaqueContext::canAppend(const OpaqueShape& opaqueShape, const MCState& state,
                              const Brush& brush) {
  if (state.clip != pendingState.clip || state.matrix != pendingState.matrix) {
    return false;
  }
  if (pendingBrushes.empty() || brush.maskFilter != pendingBrushes.back().maskFilter) {
    return false;
  }
  return opaqueShape == pendingShape;
}

Rect GetGlobalBounds(const MCState& state, const Rect& localBounds) {
  auto globalBounds = state.matrix.mapRect(localBounds);
  if (!state.clip.isInverseFillType()) {
    if (!globalBounds.intersect(state.clip.getBounds())) {
      return Rect::MakeEmpty();
    }
  }
  return globalBounds;
}

void OpaqueContext::flushPendingShape(const OpaqueShape& opaqueShape, const MCState& state,
                                      const Brush& brush) {
  PendingShapeAutoReset autoReset(this, opaqueShape, state, brush);
  if (pendingShape.type == OpaqueShape::Type::None) {
    return;
  }
  Rect localBounds = pendingShape.getBounds();
  if (pendingShape.hasStroke) {
    ApplyStrokeToBounds(pendingShape.stroke, &localBounds);
  }
  auto globalBounds = GetGlobalBounds(pendingState, localBounds);
  bool fillIsFull = false;
  if (containOpaqueBound(globalBounds) && !pendingShape.isInverseFillType()) {
    return;
  }
  for (auto& pendingBrush : pendingBrushes) {
    fillIsFull =
        fillIsFull || ((pendingBrush.shader == nullptr || !pendingBrush.shader->isAImage()) &&
                       !pendingBrush.maskFilter);
    pendingShape.draw(pictureContext, pendingState, pendingBrush);
  }
  if (fillIsFull && pendingState.matrix.rectStaysRect() &&
      pendingShape.type < OpaqueShape::Type::Path && !pendingShape.isInverseFillType() &&
      !pendingShape.hasStroke) {
    if (pendingShape.type == OpaqueShape::Type::Rect) {
      mergeOpaqueBound(globalBounds);
    } else if (pendingShape.type == OpaqueShape::Type::RRect) {
      localBounds.inset(pendingShape.rRect.radii.x, pendingShape.rRect.radii.y);
      if (localBounds.isSorted()) {
        globalBounds = GetGlobalBounds(pendingState, localBounds);
        mergeOpaqueBound(globalBounds);
      }
    }
  }
}

Brush GetOpaqueBrush(const Brush& brush) {
  Color color;
  if (brush.shader && !brush.shader->asColor(&color)) {
    auto opaqueBrush = brush;
    opaqueBrush.colorFilter = ColorFilter::AlphaThreshold(OPAQUE_THRESHOLD);
    return opaqueBrush;
  }
  // Src + coverage AA may cause edge artifacts, use SrcOver instead.
  auto opaqueBrush = Brush(Color::White(), BlendMode::SrcOver, brush.antiAlias);
  opaqueBrush.maskFilter = brush.maskFilter;
  return opaqueBrush;
}

void OpaqueContext::appendFill(const Brush& brush) {
  auto& pendingShader = pendingBrushes.back().shader;
  if (pendingShader == nullptr) {
    return;
  }
  auto opaqueBrush = GetOpaqueBrush(brush);
  if (opaqueBrush.shader == nullptr) {
    pendingBrushes = {opaqueBrush};
    return;
  }
  pendingBrushes.emplace_back(opaqueBrush);
}

void OpaqueContext::resetPendingShape(const OpaqueShape& opaqueShape, const MCState& state,
                                      const Brush& brush) {
  pendingShape = opaqueShape;
  pendingState = state;
  pendingBrushes = {GetOpaqueBrush(brush)};
}

bool OpaqueContext::OpaqueShape::isInverseFillType() const {
  if (type < Type::Path) {
    return false;
  }
  if (type == Type::Path) {
    return path.isInverseFillType();
  }
  return shape->isInverseFillType();
}

Rect OpaqueContext::OpaqueShape::getBounds() const {
  switch (type) {
    case Type::Fill: {
      return Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    }
    case Type::Rect: {
      return rect;
    }
    case Type::RRect: {
      return rRect.rect;
    }
    case Type::Path: {
      return path.getBounds();
    }
    case Type::Shape: {
      return shape->getBounds();
    }
    default:
      break;
  }
  return Rect::MakeEmpty();
}

void OpaqueContext::OpaqueShape::draw(PictureContext& context, const MCState& state,
                                      const Brush& brush) const {
  const Stroke* strokePtr = hasStroke ? &stroke : nullptr;
  switch (type) {
    case Type::Fill: {
      context.drawFill(brush);
      break;
    }
    case Type::Rect: {
      context.drawRect(rect, state, brush, strokePtr);
      break;
    }
    case Type::RRect: {
      context.drawRRect(rRect, state, brush, strokePtr);
      break;
    }
    case Type::Path: {
      context.drawPath(path, state, brush);
      break;
    }
    case Type::Shape: {
      context.drawShape(shape, state, brush, strokePtr);
      break;
    }
    default:
      break;
  }
}

bool OpaqueContext::OpaqueShape::operator==(const OpaqueShape& other) const {
  if (type != other.type || hasStroke != other.hasStroke) {
    return false;
  }
  if (hasStroke && stroke != other.stroke) {
    return false;
  }
  switch (type) {
    case Type::Rect:
      return rect == other.rect;
    case Type::RRect:
      return rRect.rect == other.rRect.rect && rRect.radii == other.rRect.radii;
    case Type::Path:
      return path == other.path;
    case Type::Shape:
      return shape == other.shape;
    default:
      break;
  }
  return true;
}

OpaqueContext::OpaqueShape& OpaqueContext::OpaqueShape::operator=(const OpaqueShape& other) {
  if (this == &other) return *this;
  switch (type) {
    case Type::Path:
      path.~Path();
      break;
    case Type::Shape:
      shape.~shared_ptr<Shape>();
      break;
    default:
      break;
  }
  type = other.type;
  hasStroke = other.hasStroke;
  stroke = other.stroke;
  switch (type) {
    case Type::Rect:
      new (&rect) Rect(other.rect);
      break;
    case Type::RRect:
      new (&rRect) RRect(other.rRect);
      break;
    case Type::Path:
      new (&path) Path(other.path);
      break;
    case Type::Shape:
      new (&shape) std::shared_ptr<Shape>(other.shape);
      break;
    case Type::Fill:
    case Type::None:
      break;
  }
  return *this;
}

}  // namespace tgfx
