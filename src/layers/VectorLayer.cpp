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

#include "tgfx/layers/VectorLayer.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "tgfx/layers/LayerRecorder.h"
#include "tgfx/layers/layerstyles/StyledShape.h"
#include "tgfx/layers/vectors/SolidColor.h"
#include "vectors/Painter.h"
#include "vectors/VectorContext.h"

namespace tgfx {

// Returns true when the painter's color source is a fully transparent solid color, which
// contributes no visible content.
static inline bool HasTransparentSolidColor(const Painter* painter) {
  const auto* colorSource = painter->colorSource.get();
  if (colorSource == nullptr || Types::Get(colorSource) != Types::ColorSourceType::SolidColor) {
    return false;
  }
  return FloatNearlyZero(static_cast<const SolidColor*>(colorSource)->color().alpha);
}

// Returns true when one of the painters records the geometry. A captured geometry is drawn through
// its painter, so it is not part of the bare geometry the layer records on its own.
static bool IsCapturedByAnyPainter(const Geometry* geometry,
                                   const std::vector<std::unique_ptr<Painter>>& painters) {
  for (const auto& painter : painters) {
    DEBUG_ASSERT(painter != nullptr);
    for (const auto* captured : painter->geometries) {
      if (captured == geometry) {
        return true;
      }
    }
  }
  return false;
}

static std::optional<StyledShape> MakeStyledShape(const std::vector<const Painter*>& painters);

std::shared_ptr<VectorLayer> VectorLayer::Make() {
  return std::shared_ptr<VectorLayer>(new VectorLayer());
}

VectorLayer::~VectorLayer() {
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    detachProperty(element.get());
  }
}

void VectorLayer::setContents(std::vector<std::shared_ptr<VectorElement>> value) {
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    detachProperty(element.get());
  }
  _contents.clear();
  for (auto& element : value) {
    if (element == nullptr) {
      continue;
    }
    attachProperty(element.get());
    _contents.push_back(std::move(element));
  }
  invalidateContent();
}

void VectorLayer::onUpdateContent(LayerRecorder* recorder) {
  ownGeometries = {};
  ownShape = std::nullopt;
  if (_contents.empty()) {
    return;
  }
  VectorContext context = {};
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    if (element->enabled()) {
      element->apply(&context);
    }
  }
  // Cache the geometries no painter captured so the own geometry shape can still include them. The
  // set is taken after every element applied, since MergePath clears geometries and painters while
  // Repeater clones and reorders them.
  for (const auto& geometry : context.geometries) {
    DEBUG_ASSERT(geometry != nullptr);
    if (IsCapturedByAnyPainter(geometry.get(), context.painters)) {
      continue;
    }
    auto shape = geometry->getShape();
    if (shape == nullptr) {
      continue;
    }
    ownGeometries.push_back({std::move(shape), geometry->matrix});
  }
  // Describe the own geometry while the context is alive: the shape needs the same painters and
  // geometries it was taken from, so re-applying every element per query would be pure waste.
  if (!ownGeometries.empty()) {
    // Geometry no painter captured has an exact outline only when it is the layer's whole own
    // geometry: a SDF built from it would otherwise disagree with the recorded raster, which also
    // holds the painters, so those cases let the style take its alpha-mask path instead.
    if (context.painters.empty() && ownGeometries.size() == 1) {
      const auto& entry = ownGeometries.front();
      auto shape = Shape::ApplyMatrix(entry.shape, entry.matrix);
      if (shape != nullptr) {
        ownShape = StyledShape::Make(shape, StyledShapeType::Fill, 0.0f, StrokeAlign::Center);
      }
    }
  } else {
    // The own geometry keeps every painter, including the 0-alpha ones, so its shape must keep that
    // same painter set or it would describe fewer painters than the raster it is paired with.
    std::vector<const Painter*> painters = {};
    for (const auto& painter : context.painters) {
      DEBUG_ASSERT(painter != nullptr);
      painters.push_back(painter.get());
    }
    ownShape = MakeStyledShape(painters);
  }
  // Render all painters
  for (const auto& painter : context.painters) {
    painter->draw(recorder);
  }
}

Rect VectorLayer::onGetOwnGeometryBounds() {
  Rect bounds = Rect::MakeEmpty();
  for (const auto& geometry : ownGeometries) {
    auto shapeBounds = geometry.shape->getBounds();
    shapeBounds = geometry.matrix.mapRect(shapeBounds);
    bounds.join(shapeBounds);
  }
  return bounds;
}

void VectorLayer::onDrawOwnGeometry(Canvas* canvas) {
  if (ownGeometries.empty()) {
    return;
  }
  Paint paint = {};
  paint.setColor(Color::White());
  for (const auto& geometry : ownGeometries) {
    AutoCanvasRestore autoRestore(canvas);
    canvas->concat(geometry.matrix);
    canvas->drawShape(geometry.shape, paint);
  }
}

std::optional<StyledShape> VectorLayer::onGetContentShape() {
  return computeContentShape();
}

std::optional<StyledShape> VectorLayer::onGetOwnGeometryShape() {
  return ownShape;
}

// Simplifies a painter set that describes a single geometry with a uniform style into one exact
// outline. Anything else has no exact outline: nullopt (consumers fall back to non-vector paths --
// glass uses the content alpha, spread uses the content bounds).
static std::optional<StyledShape> MakeStyledShape(const std::vector<const Painter*>& painters) {
  Geometry* sharedGeometry = nullptr;
  auto hasFill = false;
  std::optional<PainterStyle> strokeStyle = std::nullopt;
  for (const auto* painter : painters) {
    if (painter->geometries.size() != 1) {
      return std::nullopt;
    }
    if (sharedGeometry == nullptr) {
      sharedGeometry = painter->geometries[0];
    } else if (painter->geometries[0] != sharedGeometry) {
      return std::nullopt;
    }

    auto style = painter->getStyle();
    if (style.style == PaintStyle::Fill) {
      hasFill = true;
    } else {
      // Multiple strokes cannot be simplified to a single StyledShape.
      if (strokeStyle.has_value()) {
        return std::nullopt;
      }
      strokeStyle = style;
    }
  }
  if (sharedGeometry == nullptr) {
    return std::nullopt;
  }
  auto shape = sharedGeometry->getShape();
  if (shape == nullptr) {
    return std::nullopt;
  }
  // Baking the geometry matrix into the shape makes spread scale with the layer transform, like
  // stroke width and other in-layer measurements. This is intentional.
  shape = Shape::ApplyMatrix(shape, sharedGeometry->matrix);
  if (shape == nullptr) {
    return std::nullopt;
  }

  auto hasStroke = strokeStyle.has_value();
  auto strokeWidth = hasStroke ? strokeStyle->strokeWidth : 0.0f;
  auto strokeAlign = hasStroke ? strokeStyle->strokeAlign : StrokeAlign::Center;
  auto type = StyledShapeType::FillStroke;
  if (!hasStroke) {
    type = StyledShapeType::Fill;
  } else if (!hasFill) {
    type = StyledShapeType::Stroke;
  }
  return StyledShape::Make(shape, type, strokeWidth, strokeAlign);
}

std::optional<StyledShape> VectorLayer::computeContentShape() {
  if (_contents.empty()) {
    return std::nullopt;
  }
  VectorContext context = {};
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    if (element->enabled()) {
      element->apply(&context);
    }
  }
  // The content shape describes what the layer renders, so it drops the painters that contribute no
  // visible content.
  std::vector<const Painter*> painters = {};
  for (const auto& painter : context.painters) {
    DEBUG_ASSERT(painter != nullptr);
    if (!HasTransparentSolidColor(painter.get())) {
      painters.push_back(painter.get());
    }
  }
  return MakeStyledShape(painters);
}

}  // namespace tgfx
