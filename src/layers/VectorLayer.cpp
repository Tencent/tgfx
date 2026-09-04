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

  auto getApproximateContentShape = [this]() {
    auto contentShape = Layer::onGetContentShape();
    if (contentShape.has_value()) {
      contentShape->isExact = false;
    }
    return contentShape;
  };

  // Only a single shared geometry across all painters with a uniform stroke style can be
  // simplified to a StyledShape. The scan collects flags instead of returning early because the
  // fill surface stays derivable from the shared geometry even when the combined content falls
  // back to the approximate bounds shape below.
  Geometry* sharedGeometry = nullptr;
  auto geometryShared = true;
  auto hasFill = false;
  auto strokeCount = 0;
  std::optional<PainterStyle> strokeStyle = std::nullopt;
  for (const auto& painter : context.painters) {
    DEBUG_ASSERT(painter != nullptr);
    if (HasTransparentSolidColor(painter.get())) {
      continue;
    }
    if (painter->geometries.size() != 1 ||
        (sharedGeometry != nullptr && painter->geometries[0] != sharedGeometry)) {
      geometryShared = false;
      break;
    }
    if (sharedGeometry == nullptr) {
      sharedGeometry = painter->geometries[0];
    }

    auto style = painter->getStyle();
    if (style.style == PaintStyle::Fill) {
      hasFill = true;
    } else {
      strokeCount++;
      strokeStyle = style;
    }
  }

  std::optional<StyledShape> contentShape = std::nullopt;
  if (!geometryShared || strokeCount > 1) {
    contentShape = getApproximateContentShape();
  } else if (sharedGeometry == nullptr) {
    return std::nullopt;
  } else {
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
    contentShape = StyledShape::Make(shape, type, strokeWidth, strokeAlign);
  }

  // The fill surface only needs the shared geometry, so it stays exact regardless of how many
  // decorative strokes made the combined content fall back above.
  if (contentShape.has_value() && geometryShared && hasFill && sharedGeometry != nullptr) {
    auto fillShape = sharedGeometry->getShape();
    if (fillShape != nullptr) {
      fillShape = Shape::ApplyMatrix(fillShape, sharedGeometry->matrix);
      if (fillShape != nullptr) {
        contentShape->fillShape = fillShape;
      }
    }
  }
  return contentShape;
}

}  // namespace tgfx
