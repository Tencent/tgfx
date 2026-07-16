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

#include "gpu/DrawingManager.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

/**
 * PipelineCanonicalizer transforms arbitrary fragment processor combinations into canonical forms
 * that the precompiled shader permutation matchers can recognize. It acts as a universal fallback
 * layer: any FP tree that does not match a known canonical pattern is flattened to a TextureEffect
 * via offscreen rendering, guaranteeing zero permutation misses at the cost of an extra render
 * pass.
 *
 * This module replaces the former EffectDecomposer and the ad-hoc decompose logic in
 * OpsCompositor::addDrawOp. Unlike those approaches which only handled specific patterns,
 * PipelineCanonicalizer provides a closed guarantee: all pipelines either match a known pattern
 * (pass through unchanged) or get reduced to one that does (via flatten).
 */
class PipelineCanonicalizer {
 public:
  /**
   * Transforms the DrawOp's color fragment processor list into canonical form suitable for
   * precompiled shader matching. Must be called in OpsCompositor::addDrawOp after all FPs have
   * been added to the op, but before the op is submitted to the render task list.
   *
   * Only operates on ops with DefaultGeometryProcessor or QuadPerEdgeAAGeometryProcessor (inferred
   * from op type). Ops with custom XferProcessors or other GP types are left unchanged.
   */
  static void Canonicalize(Context* context, DrawOp* op,
                           const std::shared_ptr<RenderTargetProxy>& renderTarget);
};

}  // namespace tgfx
