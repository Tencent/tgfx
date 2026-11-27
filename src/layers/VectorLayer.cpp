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

class RecorderVectorDrawSink : public VectorDrawSink {
 public:
  explicit RecorderVectorDrawSink(LayerRecorder* recorder) : recorder(recorder) {
  }

  void drawShape(const VectorDrawOp& op) override {
    record(op);
  }

  void drawGlyphRun(const VectorDrawOp& op) override {
    record(op);
  }

  void drawOwnGeometry(const VectorDrawOp& op) override {
    recorder->setMatrix(op.matrix);
    recorder->addShape(op.shape, nullptr);
    recorder->resetMatrix();
  }

 private:
  void record(const VectorDrawOp& op) {
    recorder->setMatrix(op.matrix);
    for (const auto& paint : op.paints) {
      if (op.shape != nullptr) {
        recorder->addShape(op.shape, &paint);
      } else {
        recorder->addTextBlob(op.textBlob, &paint);
      }
    }
    recorder->resetMatrix();
  }

  LayerRecorder* recorder = nullptr;
};

class PreparedVectorDrawSink : public VectorDrawSink {
 public:
  explicit PreparedVectorDrawSink(VectorPrepareBudget* budget) : budget(budget) {
  }

  void drawShape(const VectorDrawOp& op) override {
    append(op, Kind::Shape);
  }

  void drawGlyphRun(const VectorDrawOp& op) override {
    append(op, Kind::GlyphRun);
  }

  void drawOwnGeometry(const VectorDrawOp& op) override {
    append(op, Kind::OwnGeometry);
  }

  void deliver(VectorDrawSink* sink) const {
    for (const auto& entry : entries) {
      switch (entry.kind) {
        case Kind::Shape:
          sink->drawShape(entry.op);
          break;
        case Kind::GlyphRun:
          sink->drawGlyphRun(entry.op);
          break;
        case Kind::OwnGeometry:
          sink->drawOwnGeometry(entry.op);
          break;
      }
    }
  }

 private:
  enum class Kind { Shape, GlyphRun, OwnGeometry };
  struct Entry {
    VectorDrawOp op = {};
    Kind kind = Kind::Shape;
  };

  void append(const VectorDrawOp& op, Kind kind) {
    if (!budget->consumeDrawOp(op)) {
      return;
    }
    Entry entry = {op, kind};
    entry.op.drawOrdinal = entries.size();
    entries.push_back(std::move(entry));
  }

  VectorPrepareBudget* budget = nullptr;
  std::vector<Entry> entries = {};
};

/** Preserves the ledger before the budget is destroyed, including every failed early exit. */
class VectorPrepareUsageRecorder {
 public:
  VectorPrepareUsageRecorder(const VectorPrepareBudget* budget, VectorPrepareUsage* usage)
      : budget(budget), usage(usage) {
  }

  ~VectorPrepareUsageRecorder() {
    if (usage != nullptr) {
      *usage = budget->getUsage();
    }
  }

 private:
  const VectorPrepareBudget* budget = nullptr;
  VectorPrepareUsage* usage = nullptr;
};

void VectorLayer::onUpdateContent(LayerRecorder* recorder) {
  RecorderVectorDrawSink sink(recorder);
  VectorPrepareLimits limits = {};
  prepareDraws(&sink, limits);
}

bool VectorLayer::prepareDraws(VectorDrawSink* sink, const VectorPrepareLimits& limits,
                               VectorPrepareUsage* usage) const {
  if (usage != nullptr) {
    *usage = {};
  }
  if (sink == nullptr) {
    return false;
  }
  VectorContext context(limits);
  VectorPrepareUsageRecorder usageRecorder(context.budget.get(), usage);
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    if (element->enabled() && context.prepareElement(element.get())) {
      element->apply(&context);
    }
    if (context.budget->failed) {
      break;
    }
  }
  if (context.budget->failed || !context.budget->consumeMetadata(context.geometries.size())) {
    return false;
  }
  for (size_t i = 0; i < context.geometries.size(); i++) {
    context.geometries[i]->ordinal = i;
  }
  PreparedVectorDrawSink preparedSink(context.budget.get());
  std::unordered_set<const Geometry*> capturedGeometries = {};
  capturedGeometries.reserve(context.geometries.size());
  for (const auto& painter : context.painters) {
    DEBUG_ASSERT(painter != nullptr);
    painter->draw(&preparedSink);
    if (context.budget->failed || !context.budget->consumeMetadata(painter->geometries.size())) {
      return false;
    }
    capturedGeometries.insert(painter->geometries.begin(), painter->geometries.end());
  }
  for (const auto& geometry : context.geometries) {
    DEBUG_ASSERT(geometry != nullptr);
    if (capturedGeometries.count(geometry.get()) != 0) {
      continue;
    }
    if (!context.budget->consumeDraws(1) || !context.budget->consumePoints(geometry->pointCount) ||
        !context.budget->consumeMetadata(geometry->copyProvenance.size()) ||
        !context.budget->consumeMetadata(geometry->sourceProvenance.size()) ||
        !context.budget->consumeMetadata(geometry->dependencies.size())) {
      return false;
    }
    for (const auto& origin : geometry->sourceProvenance) {
      if (!context.budget->consumeMetadata(origin.copyProvenance.size())) {
        return false;
      }
    }
    auto shape = geometry->getShape();
    if (context.budget->failed) {
      return false;
    }
    if (shape == nullptr) {
      continue;
    }
    VectorDrawOp op = {};
    op.shape = std::move(shape);
    op.matrix = geometry->matrix;
    op.finalMatrix = geometry->matrix;
    op.capturedInnerMatrix = geometry->matrix;
    op.geometry = geometry.get();
    op.geometryOrdinal = geometry->ordinal;
    op.sourceTag = geometry->sourceTag;
    op.sourceEndpoint = geometry->sourceEndpoint;
    op.copyProvenance = geometry->copyProvenance;
    op.sourceProvenance = geometry->sourceProvenance;
    op.dependencies = geometry->dependencies;
    preparedSink.drawOwnGeometry(op);
  }
  if (context.budget->failed) {
    return false;
  }
  if (usage != nullptr) {
    *usage = context.budget->getUsage();
  }
  preparedSink.deliver(sink);
  return true;
}

std::optional<StyledShape> VectorLayer::onGetContentShape() {
  if (_contents.empty()) {
    return std::nullopt;
  }
  VectorContext context = {};
  for (const auto& element : _contents) {
    DEBUG_ASSERT(element != nullptr);
    if (element->enabled() && context.prepareElement(element.get())) {
      element->apply(&context);
    }
    if (context.budget->failed) {
      break;
    }
  }
  if (context.budget->failed) {
    return std::nullopt;
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
