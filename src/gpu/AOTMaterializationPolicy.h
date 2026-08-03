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

#include <cstddef>
#include <cstdint>
#include <memory>
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
class FragmentProcessor;

/**
 * Internal render flags, carried alongside the public RenderFlags through the same uint32_t. They
 * start high enough to never collide with the public bits.
 */
class InternalRenderFlags {
 public:
  /**
   * Set while replaying a Picture into an offscreen texture. Every draw inside that replay is about
   * to be rasterized into one RGBA8 target anyway, so materializing a subtree there buys no AOT
   * match for the outer draw and only adds a second 8-bit quantization step — one that later scaling
   * of the resulting image amplifies. Materialization for matchability is skipped under this flag.
   */
  static constexpr uint32_t NestedRasterization = 1u << 16;
};

/**
 * Identifies how a child FragmentProcessor is consumed by its parent. The consumer's effect domain
 * determines whether a materialization edge is beneficial and, for neighborhood consumers, how much
 * apron the materialized texture must carry so the consumer's samples remain correct.
 */
enum class MaterializationConsumer {
  /// A pointwise blend (XfermodeFragmentProcessor child). The child is sampled at the same
  /// coordinate as the output, so no apron is required.
  PointwiseBlend,
};

/**
 * The decision produced by the materialization policy for a single child in a single consumer
 * context.
 */
struct MaterializationDecision {
  /// True when the child cannot be consumed inline at all: the consumer has no valid form that
  /// accepts it, so materializing is what makes the render correct. Holds regardless of any AOT
  /// setting.
  bool requiredForCorrectness = false;
  /// True when the child should be rendered into an offscreen texture and replaced by a plain
  /// TextureEffect so both the producer and the downstream consumer become AOT-matchable kernels.
  /// This covers matchability only, so callers may honor it only while the decomposition route is on.
  bool shouldFlatten = false;
  /// Extra ring of pixels the materialized texture must carry around the draw bounds. The draw
  /// bounds describe the area being painted, not the area the consumer samples: a coordinate landing
  /// on the boundary can resolve just outside it once the offscreen target is snapped to integers
  /// and its coordinates are normalized. Without the apron those reads clamp to the edge texels
  /// instead of returning what the subtree produced there, turning a transparent border opaque. One
  /// pixel covers that boundary case; a neighborhood consumer needs its full sampling radius.
  float apronRadius = 0.0f;
};

/**
 * The pixel geometry of one materialization edge: the area to capture and the coordinate mapping
 * that keeps the materialized subtree sampling the space it saw inline. Derived purely from the draw
 * bounds and the apron, with no GPU resource attached, so a caller that issues several passes over
 * the same area computes it once and every pass agrees on it. isEmpty() marks a degenerate area that
 * cannot back a texture.
 */
struct MaterializedGeometry {
  /// The draw bounds after the apron outset and integer snapping. Its top-left drives both the
  /// coordOffset below and the translation a consumer needs in its sampling matrix.
  Rect bounds = {};
  /// The translation handed to DrawingManager::makeFillDrawOp so the subtree keeps seeing its
  /// original coordinate space while drawing into an origin-based offscreen target.
  Point coordOffset = {};
  int width = 0;
  int height = 0;

  bool isEmpty() const {
    return width <= 0 || height <= 0;
  }

  /// The byte footprint an RGBA8 target of this size occupies, for whichever draw metric the caller
  /// maintains.
  uint64_t byteSize() const {
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
  }
};

/**
 * The offscreen target one materialization edge renders into, paired with the geometry it was
 * allocated for. A null renderTarget marks a failed allocation.
 */
struct MaterializedTarget {
  std::shared_ptr<RenderTargetProxy> renderTarget = nullptr;
  MaterializedGeometry geometry = {};
};

/**
 * AOTMaterializationPolicy is the single authority that decides where to insert materialization
 * edges to convert an otherwise unmatchable FragmentProcessor chain into a sequence of
 * AOT-matchable kernels. Callers at FragmentProcessor-build sites consult it instead of hand-coding
 * their own "is this child simple enough" predicates, so the decomposition behavior stays
 * consistent across BlendShader, ColorFilterShader, image filters, and future sites.
 */
class AOTMaterializationPolicy {
 public:
  /**
   * Decides how the given child must be treated when consumed in the given context. The result
   * separates the two independent reasons to materialize: requiredForCorrectness, when the consumer
   * cannot accept the child inline at all, and shouldFlatten, when it could but the resulting
   * permutation has no precompiled artifact.
   *
   * @param child       The child FragmentProcessor, or nullptr for an absent child.
   * @param consumer    How the child is consumed by its parent.
   * @param childIndex  The child's position in its parent (0 = src, 1 = dst for a blend).
   * @return            The materialization decision for this child.
   */
  static MaterializationDecision Evaluate(const FragmentProcessor* child,
                                          MaterializationConsumer consumer, size_t childIndex);

  /**
   * Computes the pixel geometry of one materialization edge covering drawBounds grown by apronRadius
   * on every side. A caller that renders several passes over the same area must call this once and
   * reuse the result for every pass and for the consumer's sampling matrix, so the capture area and
   * the coordinate mapping can never disagree.
   *
   * @param drawBounds   The area the materialized subtree paints, before the apron.
   * @param apronRadius  Extra ring of pixels to capture, from MaterializationDecision.
   * @return             The geometry; isEmpty() is true when the snapped area is degenerate.
   */
  static MaterializedGeometry PrepareGeometry(const Rect& drawBounds, float apronRadius);

  /**
   * Allocates one offscreen target for the given geometry. Returns a target whose renderTarget is
   * null when the geometry is degenerate or the allocation fails.
   *
   * This deliberately records no draw metrics. The two materialization routes count an edge at
   * different moments: the decomposition executor only counts one once its whole plan survives
   * strict preparation, while an inline flatten has already queued its render task and counts on
   * creation. Each caller reports its own edge.
   */
  static MaterializedTarget AllocateTarget(Context* context, const MaterializedGeometry& geometry);

  /**
   * Convenience for the single-edge case: computes the geometry and allocates its target in one
   * step. A caller issuing several passes over one area must use PrepareGeometry plus AllocateTarget
   * instead, so all passes share one geometry.
   */
  static MaterializedTarget PrepareTarget(Context* context, const Rect& drawBounds,
                                          float apronRadius);
};

}  // namespace tgfx
