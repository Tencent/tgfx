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

#include "VectorContext.h"
#include <cmath>
#include <limits>
#include "core/utils/Types.h"
#include "tgfx/layers/vectors/FillStyle.h"
#include "tgfx/layers/vectors/Gradient.h"
#include "tgfx/layers/vectors/Polystar.h"
#include "tgfx/layers/vectors/Repeater.h"
#include "tgfx/layers/vectors/ShapePath.h"
#include "tgfx/layers/vectors/StrokeStyle.h"
#include "tgfx/layers/vectors/Text.h"
#include "tgfx/layers/vectors/TextPath.h"

namespace tgfx {

VectorPrepareBudget::VectorPrepareBudget(const VectorPrepareLimits& limits) : limits(limits) {
  if (limits.maxDepth > std::numeric_limits<size_t>::max() - 8 ||
      limits.maxDraws > std::numeric_limits<size_t>::max() / (limits.maxDepth + 8)) {
    fail(VectorPrepareUsage::Limit::Draws);
    return;
  }
  metadataCeiling = limits.maxDraws * (limits.maxDepth + 8);
  const size_t counts[] = {limits.maxGeometryPoints, limits.maxGlyphs, limits.maxCopies};
  for (auto count : counts) {
    if (count > std::numeric_limits<size_t>::max() - metadataCeiling) {
      fail(VectorPrepareUsage::Limit::Draws);
      return;
    }
    metadataCeiling += count;
  }
}

void VectorPrepareBudget::fail(VectorPrepareUsage::Limit kind) {
  if (!failed) {
    failedLimit = kind;
  }
  failed = true;
}

bool VectorPrepareBudget::consume(size_t* counter, size_t count, size_t limit,
                                  VectorPrepareUsage::Limit kind) {
  if (failed || *counter > limit || count > limit - *counter) {
    fail(kind);
    return false;
  }
  *counter += count;
  return true;
}

bool VectorPrepareBudget::consumeDraws(size_t count) {
  return consume(&draws, count, limits.maxDraws, VectorPrepareUsage::Limit::Draws);
}
bool VectorPrepareBudget::consumeGeometries(size_t count) {
  return consume(&geometries, count, limits.maxDraws, VectorPrepareUsage::Limit::Draws);
}
bool VectorPrepareBudget::consumePainters(size_t count) {
  return consume(&painters, count, limits.maxDraws, VectorPrepareUsage::Limit::Draws);
}
bool VectorPrepareBudget::consumeCaptures(size_t count) {
  return consume(&captures, count, limits.maxDraws, VectorPrepareUsage::Limit::Draws);
}
bool VectorPrepareBudget::consumePoints(size_t count) {
  return consume(&points, count, limits.maxGeometryPoints,
                 VectorPrepareUsage::Limit::GeometryPoints);
}
bool VectorPrepareBudget::consumeGlyphs(size_t count) {
  return consume(&glyphs, count, limits.maxGlyphs, VectorPrepareUsage::Limit::Glyphs);
}
bool VectorPrepareBudget::consumeCopies(size_t count) {
  return consume(&copies, count, limits.maxCopies, VectorPrepareUsage::Limit::Copies);
}
bool VectorPrepareBudget::consumeMetadata(size_t count) {
  return consume(&metadata, count, metadataCeiling, VectorPrepareUsage::Limit::Draws);
}

bool VectorPrepareBudget::multiply(size_t first, size_t second, size_t* result) {
  if (failed || (second != 0 && first > std::numeric_limits<size_t>::max() / second)) {
    fail(VectorPrepareUsage::Limit::None);
    return false;
  }
  *result = first * second;
  return true;
}

bool VectorPrepareBudget::enterGroup() {
  if (failed || depth >= limits.maxDepth) {
    fail(VectorPrepareUsage::Limit::Depth);
    return false;
  }
  depth++;
  if (depth > peakDepth) {
    peakDepth = depth;
  }
  return true;
}

void VectorPrepareBudget::leaveGroup() {
  DEBUG_ASSERT(depth > 0);
  depth--;
}

bool VectorPrepareBudget::consumeColorSource(const ColorSource* source) {
  if (source == nullptr || failed) {
    failed = true;
    return false;
  }
  if (Types::Get(source) == Types::ColorSourceType::Gradient) {
    const auto* gradient = static_cast<const Gradient*>(source);
    return consumePoints(gradient->colors().size()) && consumePoints(gradient->positions().size());
  }
  return true;
}

bool VectorPrepareBudget::consumeDrawOp(const VectorDrawOp& op) {
  if (!consumeMetadata(1) || !consumeMetadata(op.paints.size()) ||
      !consumeMetadata(op.paintSnapshots.size()) || !consumeMetadata(op.copyProvenance.size()) ||
      !consumeMetadata(op.sourceProvenance.size()) || !consumeMetadata(op.dependencies.size())) {
    return false;
  }
  for (const auto& origin : op.sourceProvenance) {
    if (!consumeMetadata(origin.copyProvenance.size())) {
      return false;
    }
  }
  for (const auto& snapshot : op.paintSnapshots) {
    if (!consumePoints(snapshot.colorSource.colors.size()) ||
        !consumePoints(snapshot.colorSource.positions.size()) ||
        !consumeMetadata(snapshot.alphaFactors.size())) {
      return false;
    }
  }
  return true;
}

bool VectorPrepareBudget::consumeTextPoints(const TextBlob* blob, size_t* pointCount) {
  if (failed) {
    return false;
  }
  size_t total = 0;
  for (const auto& run : *blob) {
    if (!run.font.hasOutlines()) {
      continue;
    }
    for (size_t i = 0; i < run.glyphCount; i++) {
      Path path = {};
      if (!run.font.getPath(run.glyphs[i], &path)) {
        continue;
      }
      const auto count = static_cast<size_t>(path.countPoints());
      if (!consumePoints(count)) {
        return false;
      }
      total += count;
    }
  }
  if (pointCount != nullptr) {
    *pointCount = total;
  }
  return true;
}

bool VectorPrepareBudget::consumeTextGlyphs(const TextBlob* blob) {
  for (const auto& run : *blob) {
    if (!consumeGlyphs(run.glyphCount)) {
      return false;
    }
  }
  return !failed;
}

bool VectorPrepareBudget::consumeDashPattern(size_t count) {
  size_t normalized = 0;
  size_t copiedEntries = 0;
  if (!multiply(count, count % 2 == 0 ? 1 : 2, &normalized) ||
      normalized > static_cast<size_t>(std::numeric_limits<int>::max()) ||
      !multiply(normalized, count % 2 == 0 ? 2 : 3, &copiedEntries)) {
    fail(VectorPrepareUsage::Limit::GeometryPoints);
    return false;
  }
  return consumePoints(count) && consumePoints(copiedEntries);
}

VectorPrepareUsage VectorPrepareBudget::getUsage() const {
  VectorPrepareUsage usage = {};
  usage.limit = failedLimit;
  usage.draws = draws;
  usage.geometries = geometries;
  usage.painters = painters;
  usage.captures = captures;
  usage.geometryPoints = points;
  usage.glyphs = glyphs;
  usage.copies = copies;
  usage.metadataEntries = metadata;
  usage.peakDepth = peakDepth;
  return usage;
}

VectorContext::VectorContext() : VectorContext(VectorPrepareLimits{}) {
}

VectorContext::VectorContext(const VectorPrepareLimits& limits)
    : budget(std::make_shared<VectorPrepareBudget>(limits)) {
}

VectorContext::VectorContext(std::shared_ptr<VectorPrepareBudget> budget)
    : budget(std::move(budget)) {
}

bool VectorContext::prepareElement(const VectorElement* element) {
  if (budget->failed) {
    return false;
  }
  preparedEndpoint = element;
  preparedPoints = 0;
  preparedGlyphs = 0;
  bool producesGeometry = false;
  switch (element->type()) {
    case VectorElement::Type::Rectangle:
      preparedPoints = 17;
      producesGeometry = true;
      break;
    case VectorElement::Type::Ellipse:
      preparedPoints = 13;
      producesGeometry = true;
      break;
    case VectorElement::Type::Polystar: {
      const auto* star = static_cast<const Polystar*>(element);
      const double points = star->pointCount();
      if (!std::isfinite(points) || points > std::numeric_limits<int>::max() / 2) {
        budget->failed = true;
        return false;
      }
      const bool isStar = star->polystarType() == PolystarType::Star;
      const double count = points > 0 ? (isStar ? std::ceil(points) : std::floor(points)) : 0;
      const size_t vertices = static_cast<size_t>(count);
      if (!budget->multiply(vertices, isStar ? 6 : 3, &preparedPoints) ||
          preparedPoints == std::numeric_limits<size_t>::max()) {
        budget->failed = true;
        return false;
      }
      if (points > 0) {
        preparedPoints++;
      }
      producesGeometry = true;
      break;
    }
    case VectorElement::Type::ShapePath: {
      const auto& path = static_cast<const ShapePath*>(element)->path();
      if (path.isEmpty() && !path.isInverseFillType()) {
        return true;
      }
      preparedPoints = static_cast<size_t>(path.countPoints());
      producesGeometry = true;
      break;
    }
    case VectorElement::Type::Text: {
      const auto blob = static_cast<const Text*>(element)->textBlob();
      if (blob == nullptr) {
        return true;
      }
      for (const auto& run : *blob) {
        if (run.glyphCount > budget->limits.maxGlyphs - preparedGlyphs) {
          budget->fail(VectorPrepareUsage::Limit::Glyphs);
          return false;
        }
        preparedGlyphs += run.glyphCount;
      }
      if (budget->geometries >= budget->limits.maxDraws) {
        budget->fail(VectorPrepareUsage::Limit::Draws);
        return false;
      }
      if (!budget->consumeGlyphs(preparedGlyphs)) {
        return false;
      }
      return budget->consumeTextPoints(blob.get(), &preparedPoints);
    }
    case VectorElement::Type::TextPath:
      preparedPoints =
          static_cast<size_t>(static_cast<const TextPath*>(element)->path().countPoints());
      break;
    case VectorElement::Type::Repeater: {
      const auto* repeater = static_cast<const Repeater*>(element);
      const double copies = repeater->copies();
      if ((geometries.empty() && painters.empty()) || copies <= 0) {
        return true;
      }
      if (!std::isfinite(copies) || !std::isfinite(repeater->offset()) ||
          copies > std::numeric_limits<int>::max()) {
        budget->failed = true;
        return false;
      }
      if (copies > 0 && std::ceil(copies) > static_cast<double>(budget->limits.maxCopies)) {
        budget->fail(VectorPrepareUsage::Limit::Copies);
        return false;
      }
      break;
    }
    case VectorElement::Type::FillStyle:
      if (!geometries.empty() && !budget->consumeColorSource(
                                     static_cast<const FillStyle*>(element)->colorSource().get())) {
        return false;
      }
      break;
    case VectorElement::Type::StrokeStyle:
      if (!geometries.empty() && static_cast<const StrokeStyle*>(element)->strokeWidth() > 0 &&
          !budget->consumeColorSource(
              static_cast<const StrokeStyle*>(element)->colorSource().get())) {
        return false;
      }
      break;
    case VectorElement::Type::TrimPath:
    case VectorElement::Type::RoundCorner:
    case VectorElement::Type::MergePath:
    case VectorElement::Type::VectorGroup:
    case VectorElement::Type::TextModifier:
      break;
  }
  if (producesGeometry && budget->geometries >= budget->limits.maxDraws) {
    budget->fail(VectorPrepareUsage::Limit::Draws);
    return false;
  }
  return budget->consumePoints(preparedPoints) && budget->consumeGlyphs(preparedGlyphs);
}

void VectorContext::addShape(std::shared_ptr<Shape> shape, const void* sourceTag) {
  addShape(std::move(shape), Point::Zero(), sourceTag);
}

void VectorContext::addShape(std::shared_ptr<Shape> shape, const Point& position,
                             const void* sourceTag) {
  if (shape == nullptr || !budget->consumeGeometries(1) || !budget->consumeMetadata(1)) {
    return;
  }
  auto geometry = std::make_unique<Geometry>();
  geometry->shape = std::move(shape);
  geometry->matrix = Matrix::MakeTrans(position.x, position.y);
  geometry->sourceTag = sourceTag;
  geometry->sourceEndpoint = preparedEndpoint;
  geometry->pointCount = preparedPoints;
  geometry->budget = budget.get();
  geometry->sourceProvenance.push_back({sourceTag, {}, preparedEndpoint});
  geometries.push_back(std::move(geometry));
}

void VectorContext::addTextBlob(std::shared_ptr<TextBlob> blob, const Point& position,
                                const std::vector<Point>& textAnchors, const void* sourceTag) {
  if (blob == nullptr || !budget->consumeGeometries(1) ||
      !budget->consumeMetadata(textAnchors.size()) || !budget->consumeMetadata(1)) {
    return;
  }
  auto geometry = std::make_unique<Geometry>();
  geometry->textBlob = std::move(blob);
  geometry->matrix = Matrix::MakeTrans(position.x, position.y);
  geometry->textAnchors = textAnchors;
  geometry->sourceTag = sourceTag;
  geometry->sourceEndpoint = preparedEndpoint;
  geometry->glyphCount = preparedGlyphs;
  geometry->pointCount = preparedPoints;
  geometry->budget = budget.get();
  geometry->sourceProvenance.push_back({sourceTag, {}, preparedEndpoint});
  geometries.push_back(std::move(geometry));
}

std::vector<Geometry*> VectorContext::getShapeGeometries() {
  std::vector<Geometry*> result = {};
  if (!budget->consumeMetadata(geometries.size())) {
    return result;
  }
  result.reserve(geometries.size());
  for (auto& geometry : geometries) {
    if (geometry->hasText()) {
      geometry->convertToShape();
      if (budget->failed) {
        return {};
      }
    }
    result.push_back(geometry.get());
  }
  return result;
}

std::vector<Geometry*> VectorContext::getGlyphGeometries() {
  std::vector<Geometry*> result = {};
  if (!budget->consumeMetadata(geometries.size())) {
    return result;
  }
  result.reserve(geometries.size());
  for (auto& geometry : geometries) {
    if (!geometry->hasText()) {
      continue;
    }
    if (geometry->textBlob != nullptr) {
      geometry->expandToGlyphs();
      if (budget->failed) {
        return {};
      }
    }
    result.push_back(geometry.get());
  }
  return result;
}

bool VectorContext::addDependency(Geometry* geometry, VectorGeometryDependency::Kind kind,
                                  const void* sourceTag, const void* runtimeEndpoint) {
  if (!budget->consumeMetadata(1)) {
    return false;
  }
  geometry->dependencies.push_back({kind, sourceTag, runtimeEndpoint});
  return true;
}

}  // namespace tgfx
