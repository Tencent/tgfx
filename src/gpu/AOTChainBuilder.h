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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

/**
 * Rebuilds concrete FragmentProcessors from a decomposed AOT plan: the fused chain kernel for
 * PointwiseChain passes, the fused perlin kernel for PerlinNoiseFill passes and the linear
 * texture-plus-tail-op sequence for PointwiseTail passes. Used by AOTPlanExecutor's task assembly
 * and by the direct-draw decomposition route in StandardDrawOp::prepare.
 */
class AOTChainBuilder {
 public:
  /** Whether the chain kernel's tiled path can execute the given shader wrap mode directly. */
  static bool IsChainCompatibleTiledMode(TiledTextureShaderMode mode);

  /// Compares only the recipe fields the chain kernel's single tiled uniform block consumes.
  /// Two shader-tiled leaves with identical recipes can share the block (the image-filter shape:
  /// source and shadow children sample the same filter domain); different recipes cannot.
  static bool SameTiledShaderRecipe(const AOTTiledTextureRecipe& a, const AOTTiledTextureRecipe& b);

  /**
   * Rebuilds the color processor of one plan pass: a fused chain/perlin kernel for the matching
   * single-pass kernels, or the pass's source node followed by its pointwise tail slots when
   * sourceOverride is nullptr. Returns nullptr when the pass shape cannot be rebuilt.
   */
  static PlacementPtr<FragmentProcessor> BuildFPForPass(
      BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
      PlacementPtr<FragmentProcessor> sourceOverride);

  /**
   * Flattens a single-pass PointwiseChain plan into the fused chain processor, or returns nullptr
   * if the pass shape exceeds what the chain kernel can represent. The returned processor carries
   * the whole DAG in uniforms, so a draw whose color processors are replaced by it evaluates the
   * same pixels through the precompiled PointwiseChainShader.
   *
   * coverageFPs, when non-empty, folds the draw's coverage into the chain. A single FP uses the
   * narrow forms first (a bare device-space AA RectEffect becomes an AARectCoverage slot, an alpha-only
   * DeviceSpaceTextureEffect becomes the mask child, Compose(mask, rect) becomes both); anything
   * else is lowered as a general pointwise coverage subtree rooted at the GP coverage, which the
   * kernel multiplies into the color result instead of the plain coverage modulation. Two FPs are
   * accepted in the form [lowerable subtree, alpha-only device mask], folding to subtree + mask
   * child. Any other coverage form returns nullptr.
   */
  static PlacementPtr<FragmentProcessor> BuildChainProcessor(
      BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
      const std::vector<const FragmentProcessor*>& coverageFPs = {},
      bool coverageLeafFromUVCoord = false);

  /**
   * Rebuilds the fused perlin-source processor of a single-pass PerlinNoiseFill plan: the noise
   * source plus up to three folded pointwise/const/blend operator slots. Returns nullptr when the
   * pass is not a valid PerlinNoiseFill pass.
   */
  static PlacementPtr<FragmentProcessor> BuildPerlinNoiseFP(BlockAllocator* allocator,
                                                            const AOTEffectGraph& graph,
                                                            const AOTPassDescriptor& pass);
};
}  // namespace tgfx
