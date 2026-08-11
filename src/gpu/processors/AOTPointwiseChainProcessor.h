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

#include <array>
#include <vector>
#include "gpu/AOTEffect.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

/// The runtime operator of one chain slot. The integer values are part of the precompiled kernel
/// ABI: they must match the OP_* constants in pointwise_chain.inc.
enum class AOTChainOp : int {
  ColorMatrix = 0,
  Luma = 1,
  AlphaThreshold = 2,
  ColorSpaceXform = 3,
  None = 4,
  Texture = 5,
  ConstColor = 6,
  Blend = 7,
  AARectCoverage = 8,
  Gradient = 9,
};

/// One node of a pointwise DAG after flattening into the fused kernel's slot array. in0/in1 are
/// slot indices of this node's inputs: -1 selects the geometry color (the Color uniform), -2 marks
/// an unused input. Only Blend uses both inputs.
struct AOTChainSlot {
  AOTChainOp op = AOTChainOp::None;
  int in0 = -2;
  int in1 = -2;
  // OP_TEXTURE only: 1 modulates the sample by the geometry color alpha (color sources fed
  // directly from the paint color), 0 samples raw (blend operands such as coverage masks, which
  // the runtime emits without input-alpha modulation).
  int textureModulate = 0;
  // OP_TEXTURE only: 1 marks an alpha-only leaf (R8 on Metal, so the sampler returns (r,0,0,1)).
  // The kernel splats .r into all channels so the alpha channel carries the mask value, matching
  // the runtime TextureEffect readback. Uploaded as bit 1 of the slot selector; textureModulate
  // remains bit 0.
  int textureAlphaOnly = 0;
  AOTColorMatrixParameters colorMatrix = {};
  AOTLumaParameters luma = {};
  AOTAlphaThresholdParameters alphaThreshold = {};
  AOTColorSpaceXformParameters colorSpaceXform = {};
  AOTConstColorParameters constColor = {};
  AOTBlendParameters blend = {};
  // OP_AARECT_COVERAGE only: the clip rect in device coordinates. The kernel reads the chain-wide
  // CoverageRect uniform, so at most one slot per chain may carry this op (enforced by the
  // builder).
  AOTRectCoverageParameters rectCoverage = {};
  // OP_GRADIENT only: layout/colorizer parameters. The kernel reads chain-wide Gradient*
  // uniforms, so at most one slot per chain may carry this op (enforced by the builder).
  AOTGradientParameters gradient = {};
};

/**
 * A FragmentProcessor that evaluates an entire pointwise DAG in one pass through the precompiled
 * PointwiseChainShader. Texture leaves are registered as child processors in slot order: child k
 * pairs with slot k and samples TextureSampler_k, which keeps sampler indexing static in the
 * kernel. Everything else about the DAG — op types, wiring, blend modes, operator parameters —
 * travels in uniforms, so one program variant serves any topology with the same leaf count.
 *
 * A chain may additionally carry one device-space alpha-mask child (registered last, after all
 * leaves): it is not a DAG node; the kernel samples it after the DAG and multiplies the result
 * before the coverage/XP stage, matching the legacy blend kernel's mask application point.
 */
class AOTPointwiseChainProcessor : public FragmentProcessor {
 public:
  static constexpr size_t MaxSlots = 16;

  static PlacementPtr<AOTPointwiseChainProcessor> Make(
      BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
      const std::vector<AOTChainSlot>& slots, size_t rootSlot, int tiledLeafIndex = -1,
      const AOTTiledTextureRecipe* tiledRecipe = nullptr,
      PlacementPtr<FragmentProcessor> maskChild = nullptr);

  AOTPointwiseChainProcessor(std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
                             const std::vector<AOTChainSlot>& slots, size_t rootSlot,
                             int tiledLeafIndex, const AOTTiledTextureRecipe* tiledRecipe,
                             PlacementPtr<FragmentProcessor> maskChild);

  std::string name() const override {
    return "AOTPointwiseChainProcessor";
  }

  size_t leafCount() const {
    return numChildProcessors() - (hasMask() ? 1 : 0);
  }

  // Whether the chain carries a device-space alpha-mask child (always the last child).
  bool hasMask() const {
    return hasMaskChild;
  }

  size_t slotCount() const {
    return _slotCount;
  }

  size_t root() const {
    return rootSlot;
  }

  // Index of the leaf that needs shader-side tiling (wrap/border emulation), or -1 when every
  // leaf is plain. At most one tiled leaf is supported per chain.
  int tiledLeaf() const {
    return tiledLeafIndex;
  }

  const AOTTiledTextureRecipe& tiledRecipe() const {
    return _tiledRecipe;
  }

  const AOTChainSlot& slot(size_t index) const {
    return slots[index];
  }

  void emitCode(EmitArgs& args) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

 private:
  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;

  size_t _slotCount = 0;
  size_t rootSlot = 0;
  int tiledLeafIndex = -1;
  AOTTiledTextureRecipe _tiledRecipe = {};
  std::array<AOTChainSlot, MaxSlots> slots = {};
  // Kept only to mark the mask's presence; the mask itself is the last registered child.
  bool hasMaskChild = false;
  // Always registered as this processor's own coord transform (index 0, ahead of the leaf
  // transforms) so the gradient coordinate mapping shares the GP-written transform path. A chain
  // without a gradient slot exposes a default identity transform here.
  CoordTransform gradientCoordTransform = {};
};

}  // namespace tgfx
