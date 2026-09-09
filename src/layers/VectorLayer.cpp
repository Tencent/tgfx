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

  // Only a single shared geometry across all painters can be simplified to a StyledShape.
  // Stacked strokes keep the exact outline when they share the same width and alignment
  // (rendering identical to one stroke); differing strokes and non-shared geometries drop it
  // (nullopt).
  Geometry* sharedGeometry = nullptr;
  auto geometryShared = true;
  auto hasFill = false;
  auto uniformStroke = true;
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
      // Stacked strokes keep a single exact outline only when they all share the same width and
      // alignment (rendering identical to one stroke); any difference drops it (nullopt).
      if (strokeStyle.has_value() && (strokeStyle->strokeWidth != style.strokeWidth ||
                                      strokeStyle->strokeAlign != style.strokeAlign)) {
        uniformStroke = false;
      }
      if (!strokeStyle.has_value()) {
        strokeStyle = style;
      }
    }
  }

  std::optional<StyledShape> contentShape = std::nullopt;
  if (geometryShared && sharedGeometry != nullptr) {
    if (uniformStroke) {
      auto shape = sharedGeometry->getShape();
      if (shape != nullptr) {
        // Baking the geometry matrix into the shape makes spread scale with the layer transform,
        // like stroke width and other in-layer measurements. This is intentional.
        shape = Shape::ApplyMatrix(shape, sharedGeometry->matrix);
      }
      if (shape != nullptr) {
        auto hasStroke = strokeStyle.has_value();
        auto strokeWidth = hasStroke ? strokeStyle->strokeWidth : 0.0f;
        auto strokeAlign = hasStroke ? strokeStyle->strokeAlign : StrokeAlign::Center;
        auto type = StyledShapeType::FillStroke;
        if (!hasStroke) {
          type = StyledShapeType::Fill;
        } else if (!hasFill) {
          type = StyledShapeType::Stroke;
        }
        contentShape = StyledShape::Make(std::move(shape), type, strokeWidth, strokeAlign);
      }
    }
    // Stacked strokes with differing widths or alignments have no single exact outline: nullopt.
    // Consumers fall back to non-vector paths (glass uses the content alpha; spread uses the
    // content bounds).
  }
  // Non-shared geometries cannot produce a single exact outline: nullopt.

  return contentShape;
}

}  // namespace tgfx
