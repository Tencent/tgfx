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
 * AOTMaterializationPolicy owns the geometry and allocation of materialization edges — the pixel
 * area an edge captures and the offscreen target it renders into — so the plan executor and every
 * pass over the same area agree on one capture area and coordinate mapping.
 */
class AOTMaterializationPolicy {
 public:
  /**
   * Computes the pixel geometry of one materialization edge covering drawBounds grown by
   * apronRadius on every side, snapped out to whole pixels. A caller rendering several passes over
   * the same area computes it once and reuses it for every pass and for the consumer's sampling
   * matrix, so the capture area and the coordinate mapping cannot disagree.
   */
  static MaterializedGeometry PrepareGeometry(const Rect& drawBounds, float apronRadius);

  /**
   * Allocates one exact-fit offscreen target for the given geometry. Returns a target whose
   * renderTarget is null when the geometry is degenerate or the allocation fails. This records no
   * draw metrics: each caller reports its own edge (the plan executor once its whole plan
   * survives strict preparation, the inline flatten when it queues its render task).
   */
  static MaterializedTarget AllocateTarget(Context* context, const MaterializedGeometry& geometry);

  /** PrepareGeometry plus AllocateTarget for the single-edge case. */
  static MaterializedTarget PrepareTarget(Context* context, const Rect& drawBounds,
                                          float apronRadius);
};

}  // namespace tgfx
