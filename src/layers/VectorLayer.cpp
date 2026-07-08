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
  if (context.painters.empty()) {
    return std::nullopt;
  }

  // Only a single shared geometry across all painters with a uniform stroke style can be
  // simplified to a StyledShape.
  Geometry* sharedGeometry = nullptr;
  auto hasFill = false;
  std::optional<PainterStyle> strokeStyle = std::nullopt;
  for (const auto& painter : context.painters) {
    DEBUG_ASSERT(painter != nullptr);
    if (HasTransparentSolidColor(painter.get())) {
      continue;
    }
    if (painter->geometries.size() != 1) {
      return Layer::onGetContentShape();
    }
    if (sharedGeometry == nullptr) {
      sharedGeometry = painter->geometries[0];
    } else if (painter->geometries[0] != sharedGeometry) {
      return Layer::onGetContentShape();
    }

    auto style = painter->getStyle();
    if (style.style == PaintStyle::Fill) {
      hasFill = true;
    } else {
      // Multiple strokes cannot be simplified to a single StyledShape.
      if (strokeStyle.has_value()) {
        return Layer::onGetContentShape();
      }
      strokeStyle = style;
    }
  }

  // sharedGeometry stays null when every painter was skipped as invisible, meaning the layer has
  // no visible content.
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
