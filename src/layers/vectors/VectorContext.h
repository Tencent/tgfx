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

#pragma once

#include <memory>
#include <vector>
#include "Geometry.h"
#include "Painter.h"

namespace tgfx {

class VectorElement;

/** Shared checked counters for every group and painter in one vector traversal. */
class VectorPrepareBudget {
 public:
  /** Creates checked ceilings from limits, including bounded draw/depth metadata overhead. */
  explicit VectorPrepareBudget(const VectorPrepareLimits& limits);

  /** Reserves count paint draws before their prepared draw allocation. */
  bool consumeDraws(size_t count);
  /** Reserves count transient geometry objects before allocation. */
  bool consumeGeometries(size_t count);
  /** Reserves count painters before creation or cloning. */
  bool consumePainters(size_t count);
  /** Reserves count captured geometry pointers before vector expansion. */
  bool consumeCaptures(size_t count);
  /** Reserves count source/path points or gradient-stop entries before copying. */
  bool consumePoints(size_t count);
  /** Reserves count glyph or selector-cache entries before copying or construction. */
  bool consumeGlyphs(size_t count);
  /** Reserves count logical repeater copies before cloning any source geometry. */
  bool consumeCopies(size_t count);
  /** Reserves count owned metadata entries before expansion. */
  bool consumeMetadata(size_t count);
  /** Writes first times second into result only if the multiplication cannot overflow. */
  bool multiply(size_t first, size_t second, size_t* result);
  /** Enters a nested group only when its depth remains within the ceiling. */
  bool enterGroup();
  /** Balances a successful enterGroup after its traversal exits. */
  void leaveGroup();
  /** Reserves the supported source's stop arrays before shader or snapshot creation. */
  bool consumeColorSource(const ColorSource* source);
  /** Reserves every nested owned field of op before retaining a prepared copy. */
  bool consumeDrawOp(const VectorDrawOp& op);
  /** Reserves acquired font-outline copies under the font's system contract; pointCount may be null. */
  bool consumeTextPoints(const TextBlob* blob, size_t* pointCount);
  /** Reserves glyph wrappers before a blob is converted into vector geometry. */
  bool consumeTextGlyphs(const TextBlob* blob);
  /** Reserves dash normalization and effect interval copies before their creation. */
  bool consumeDashPattern(size_t count);
  /** Returns the cumulative reservation ledger and accepted depth peak without allocating. */
  VectorPrepareUsage getUsage() const;
  /** Records the first failed quota, leaving failures without a quota as None. */
  void fail(VectorPrepareUsage::Limit kind);

  VectorPrepareLimits limits = {};
  bool failed = false;
  size_t draws = 0;
  size_t geometries = 0;
  size_t painters = 0;
  size_t captures = 0;
  size_t points = 0;
  size_t glyphs = 0;
  size_t copies = 0;
  size_t metadata = 0;
  size_t depth = 0;
  size_t peakDepth = 0;

 private:
  bool consume(size_t* counter, size_t count, size_t limit, VectorPrepareUsage::Limit kind);
  size_t metadataCeiling = 0;
  VectorPrepareUsage::Limit failedLimit = VectorPrepareUsage::Limit::None;
};

/** Holds vector rendering state with a budget shared by all nested group contexts. */
class VectorContext {
 public:
  VectorContext();
  explicit VectorContext(const VectorPrepareLimits& limits);
  explicit VectorContext(std::shared_ptr<VectorPrepareBudget> budget);

  /** Checks element's known allocation counts and numeric loop bounds before its apply call. */
  bool prepareElement(const VectorElement* element);

  /** Adds shape with the current element's checked source point cost and opaque sourceTag. */
  void addShape(std::shared_ptr<Shape> shape, const void* sourceTag = nullptr);

  /** Adds shape at position with the current checked point cost and opaque sourceTag. */
  void addShape(std::shared_ptr<Shape> shape, const Point& position,
                const void* sourceTag = nullptr);

  /** Adds blob at position, copying textAnchors only after glyph and metadata budgets pass. */
  void addTextBlob(std::shared_ptr<TextBlob> blob, const Point& position,
                   const std::vector<Point>& textAnchors = {}, const void* sourceTag = nullptr);

  /** Returns checked shape-mode geometry pointers, converting text only within the shared budget. */
  std::vector<Geometry*> getShapeGeometries();

  /** Returns checked glyph-mode geometry pointers, expanding text within the shared budget. */
  std::vector<Geometry*> getGlyphGeometries();

  /** Adds kind's author sourceTag and runtimeEndpoint binding identities after checking metadata. */
  bool addDependency(Geometry* geometry, VectorGeometryDependency::Kind kind, const void* sourceTag,
                     const void* runtimeEndpoint);

  std::shared_ptr<VectorPrepareBudget> budget = nullptr;
  std::vector<std::unique_ptr<Geometry>> geometries = {};
  std::vector<std::unique_ptr<Painter>> painters = {};

 private:
  const void* preparedEndpoint = nullptr;
  size_t preparedPoints = 0;
  size_t preparedGlyphs = 0;
};

}  // namespace tgfx
