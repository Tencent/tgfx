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

#include <atomic>
#include <optional>
#include "gpu/proxies/GPUBufferProxy.h"
#include "tgfx/core/PathTypes.h"

namespace tgfx {
/**
 * FillTypeSlot carries the fill rule of a stencil-cover geometry from the thread that tessellates
 * it to the draw ops that consume it. The rule is only known once the outline exists, which happens
 * after the proxy that needs it has already been created, so the slot starts empty and is published
 * once. Every proxy sharing one cached geometry shares its slot, which is what lets a second draw
 * recorded before the first one is uploaded read the rule the first one produces.
 */
class FillTypeSlot {
 public:
  /**
   * Returns the published rule, or std::nullopt while the tessellator has not reported one.
   */
  std::optional<PathFillType> get() const {
    auto value = fillType.load(std::memory_order_acquire);
    if (value < 0) {
      return std::nullopt;
    }
    return static_cast<PathFillType>(value);
  }

  /**
   * Publishes the rule. The geometry it belongs to is immutable, so a later report can only repeat
   * the first one.
   */
  void set(PathFillType type) {
    fillType.store(static_cast<int>(type), std::memory_order_release);
  }

 private:
  // The rule as an int so that -1 can stand for "not reported yet".
  std::atomic<int> fillType = {-1};
};

/**
 * StencilCoverPathProxy holds the GPU vertex buffer produced by the stencil-and-cover
 * render path. The vertex stream stores per-vertex position together with the
 * implicit-curve coefficients consumed by the stencil pass; transforms and per-draw color are
 * supplied through uniforms by the draw op so multiple draws of the same shape can share the
 * same cached geometry.
 */
class StencilCoverPathProxy {
 public:
  StencilCoverPathProxy(std::shared_ptr<GPUBufferProxy> vertexBuffer,
                        std::shared_ptr<FillTypeSlot> fillType)
      : vertexBuffer(std::move(vertexBuffer)), fillType(std::move(fillType)) {
  }

  Context* getContext() const {
    return vertexBuffer ? vertexBuffer->getContext() : nullptr;
  }

  /**
   * Returns the underlying vertex buffer proxy. Two cache-hit calls to the proxy provider with
   * the same shape return geometry proxies that share the same vertex buffer proxy instance.
   */
  std::shared_ptr<GPUBufferProxy> getVertexBufferProxy() const {
    return vertexBuffer;
  }

  /**
   * Returns the underlying vertex buffer resource. May be nullptr while the upload task is
   * still pending or when the rasterizer produced no drawable geometry.
   */
  std::shared_ptr<BufferResource> getVertexBuffer() const {
    return vertexBuffer ? vertexBuffer->getBuffer() : nullptr;
  }

  /**
   * Returns the rule the vertex stream is read by, or std::nullopt while the tessellator has not
   * reported it. Reporting it is part of producing the geometry, so a draw op learns the rule from
   * the geometry it is about to draw instead of asking the shape during recording.
   */
  std::optional<PathFillType> getFillType() const {
    return fillType ? fillType->get() : std::nullopt;
  }

 private:
  std::shared_ptr<GPUBufferProxy> vertexBuffer = nullptr;
  std::shared_ptr<FillTypeSlot> fillType = nullptr;
};
}  // namespace tgfx
