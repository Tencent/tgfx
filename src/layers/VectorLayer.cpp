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
#include <unordered_set>
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
  // Render all painters
  for (const auto& painter : context.painters) {
    painter->draw(recorder);
  }
  // Geometries no painter captured are recorded without a paint: they paint nothing and stay out of
  // the shared contour, but they are still this layer's geometry. The set is taken after every
  // element applied, since MergePath clears geometries and painters while Repeater clones and
  // reorders them. A captured geometry is drawn through its painter, so it is not bare; collecting
  // the captures once keeps the comparison from being quadratic in the geometry count.
  std::unordered_set<const Geometry*> capturedGeometries = {};
  capturedGeometries.reserve(context.geometries.size());
  for (const auto& painter : context.painters) {
    DEBUG_ASSERT(painter != nullptr);
    capturedGeometries.insert(painter->geometries.begin(), painter->geometries.end());
  }
  for (const auto& geometry : context.geometries) {
    DEBUG_ASSERT(geometry != nullptr);
    if (capturedGeometries.count(geometry.get()) != 0) {
      continue;
    }
    auto shape = geometry->getShape();
    if (shape == nullptr) {
      continue;
    }
    // The recorder applies its CTM to the geometry, so the matrix stays a transform instead of
    // being baked into the shape.
    recorder->setMatrix(geometry->matrix);
    recorder->addShape(std::move(shape), nullptr);
    recorder->resetMatrix();
  }
}

std::optional<StyledShape> VectorLayer::onGetContentShape() {
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
  // The content shape describes what the layer renders, so it drops the painters whose color source
  // is a fully transparent solid color; any other effectively invisible source, a transparent
  // gradient for one, stays in.
  std::vector<const Painter*> painters = {};
  for (const auto& painter : context.painters) {
    DEBUG_ASSERT(painter != nullptr);
    if (!HasTransparentSolidColor(painter.get())) {
      painters.push_back(painter.get());
    }
  }
  // Anything that does not describe one geometry with a uniform style has no exact outline:
  // consumers of the content shape fall back to non-vector paths (spread uses the content bounds).
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

}  // namespace tgfx
