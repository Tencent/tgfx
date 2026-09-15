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
#include "gpu/shaders/KernelContract.h"

namespace tgfx {

/// The runtime operator of one chain slot. The integer values are part of the precompiled kernel
/// ABI: they must match the OP_* constants in pointwise_chain.inc, which shader_build_tool
/// validates against the shared ChainOp constants at bundle build time.
enum class AOTChainOp : int {
  ColorMatrix = ChainOp::ColorMatrix,
  Luma = ChainOp::Luma,
  AlphaThreshold = ChainOp::AlphaThreshold,
  ColorSpaceXform = ChainOp::ColorSpaceXform,
  None = ChainOp::None,
  Texture = ChainOp::Texture,
  ConstColor = ChainOp::ConstColor,
  Blend = ChainOp::Blend,
  AARectCoverage = ChainOp::AARectCoverage,
  Gradient = ChainOp::Gradient,
  LocalRectCoverage = ChainOp::LocalRectCoverage,
  RRectCoverage = ChainOp::RRectCoverage,
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
  // OP_TEXTURE only: 1 modulates the sample by the coverage unit's alpha (bit 2 of the selector).
  // Set on a leaf that is the coverage subtree's root, matching the runtime coverage-FP readback
  // (tex * coverageIn.a).
  int textureModulateUnit = 0;
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
  // OP_LOCAL_RECT_COVERAGE only: the local-space clip rect. The kernel reads the chain-wide
  // CoverageLocalRect / CoverageLocalDeviceToLocal uniforms, so at most one slot per chain may
  // carry this op (enforced by the builder).
  AOTRectCoverageParameters localRectCoverage = {};
  // OP_RRECT_COVERAGE only: the rounded-rect parameters. The kernel reads the CoverageRRect*
  // uniform arrays indexed by the slot's rrect ordinal (assigned in slot order by the builder,
  // carried in the selector's bits 16-19), so at most four such slots per chain.
  AOTRRectCoverageParameters rrectCoverage = {};
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

  // The chain kernel carries exactly one shared tiled-sampling uniform block, so at most one
  // shader-tiled leaf is expressible per chain.
  static constexpr size_t MaxShaderTiledChainLeaves = 1;

  // The single authority for the chain kernel's shared parameter-block budgets. The kernel has
  // exactly one chain-wide gradient block and gradient coordinate varying, one color-space
  // transform block, one device-space rect coverage block, one local rect coverage block, and
  // four rrect array elements. The planner (AOTPlanExecutor::CanExecute), the chain builder, and
  // this processor's constructor all consult these constants instead of hard-coding their own
  // copies, so a budget change cannot leave the three check sites disagreeing.
  static constexpr size_t MaxGradientSlots = 1;
  static constexpr size_t MaxColorSpaceXformSlots = 1;
  static constexpr size_t MaxDeviceRectCoverageSlots = 1;
  static constexpr size_t MaxLocalRectCoverageSlots = 1;
  static constexpr size_t MaxRRectCoverageSlots = 4;

  // Logical sampler children before padding. The artifacts bind 0 or 4 samplers; the builder
  // pads 1, 2 and 3 children to 4 without adding sampled DAG slots.
  static bool HasChainKernelVariant(size_t samplerChildren) {
    return samplerChildren <= static_cast<size_t>(MaxFusedAOTSamplers);
  }

  static PlacementPtr<AOTPointwiseChainProcessor> Make(
      BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
      const std::vector<AOTChainSlot>& slots, size_t rootSlot, int tiledLeafIndex = -1,
      const AOTTiledTextureRecipe* tiledRecipe = nullptr,
      PlacementPtr<FragmentProcessor> maskChild = nullptr, int coverageRootSlot = -1,
      uint32_t coordSourceMask = ~0u, PlacementPtr<FragmentProcessor> lutChild = nullptr,
      int lutLeafIndex = -1, std::vector<PlacementPtr<FragmentProcessor>> samplerPadding = {},
      bool maskChildIsPhantom = false);

  AOTPointwiseChainProcessor(std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
                             const std::vector<AOTChainSlot>& slots, size_t rootSlot,
                             int tiledLeafIndex, const AOTTiledTextureRecipe* tiledRecipe,
                             PlacementPtr<FragmentProcessor> maskChild, int coverageRootSlot,
                             uint32_t coordSourceMask, PlacementPtr<FragmentProcessor> lutChild,
                             int lutLeafIndex,
                             std::vector<PlacementPtr<FragmentProcessor>> samplerPadding,
                             bool maskChildIsPhantom);

  std::string name() const override {
    return "AOTPointwiseChainProcessor";
  }

  // Sampler-binding leaf children (DAG leaves plus phantom padding), i.e. the count the
  // TEXTURE_COUNT dimension is encoded from. The mask slot child (real or phantom) is excluded.
  size_t leafCount() const {
    return numChildProcessors() - (hasMaskSlotChild ? 1 : 0);
  }

  // Whether the chain carries a real device-space alpha-mask child. Four-leaf chains always
  // occupy the mask sampler slot (a phantom when no mask is present), so this flag only selects
  // the runtime application, not the binding layout.
  bool hasMask() const {
    return hasMaskChild;
  }

  size_t slotCount() const {
    return _slotCount;
  }

  size_t root() const {
    return rootSlot;
  }

  // Slot index of the coverage subtree's root, or -1 when the chain carries no coverage subtree.
  // When present, the kernel multiplies the color result by that slot's value instead of the
  // plain coverage modulation.
  int coverageRoot() const {
    return coverageRootSlot;
  }

  // Index of the leaf that needs shader-side tiling (wrap/border emulation), or -1 when every
  // leaf is plain. At most one tiled leaf is supported per chain.
  int tiledLeaf() const {
    return tiledLeafIndex;
  }

  // Sampler index of the LUT gradient child, or -1 when the chain has none. The LUT child binds a
  // 2D texture into a leaf slot, so a chain carrying one cannot use a rectangle (sampler2DRect)
  // leaf variant.
  int lutLeaf() const {
    return lutLeafIndex;
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
  // The mask sampler slot is occupied (real mask or phantom) — four-leaf chains always occupy it.
  bool hasMaskSlotChild = false;
  bool hasMaskChild = false;
  // Always registered as this processor's own coord transform (index 0, ahead of the leaf
  // transforms) so the gradient coordinate mapping shares the GP-written transform path. A chain
  // without a gradient slot exposes a default identity transform here.
  CoordTransform gradientCoordTransform = {};
  int coverageRootSlot = -1;
  // Per-target coordinate source for the chain vertex stage: bit k set sources leaf k from the
  // uvCoord attribute, bit 4 does the same for the gradient coordinates; clear bits source from
  // aPosition. All-ones reproduces the legacy shared-source behavior (quads, meshes); atlas text
  // sets only the atlas leaf's bit so gradients and image leaves come from position.
  uint32_t coordSourceMask = ~0u;
  // Sampler-only child for the LUT gradient colorizer, registered after the DAG leaves (and
  // counted by leafCount() so the matcher sizes TEXTURE_COUNT for it) but holding no chain slot;
  // lutLeafIndex is its sampler index, read by the kernel's OP_GRADIENT LUT branch.
  int lutLeafIndex = -1;
};

}  // namespace tgfx
