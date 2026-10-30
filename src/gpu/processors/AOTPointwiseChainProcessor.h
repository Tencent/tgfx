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
#include "gpu/AOTResourceProfile.h"
#include "gpu/aot/EffectGraph.h"
#include "gpu/aot/Program.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/shaders/KernelContract.h"

namespace tgfx {

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
  DEFINE_PROCESSOR_CLASS_ID
  // The maximum instruction storage across the shapes (the noise shape: 32; the gradient shape
  // shares the plain chain's 16). ValidateSlots checks the resolved shape's own capacity.
  static constexpr size_t MaxSlots = NoiseGraphResourceCapacity.instructions;

  // The length of the kernel's chainResults array (pointwise_fragment_main.inc). The allocator
  // reuses a register after its last read, so this bounds the simultaneously-live values of a
  // DAG, not its length. These are interpreter slots, not GPU hardware registers.
  static constexpr size_t MaxRegisters = 16;

  // The chain kernel carries two independent tiled-sampling recipes, selected by
  // TiledLeafIndex/TiledLeafIndex2, so at most this many leaves may use shader-side tiling; their
  // recipes need not match (a drop shadow's source and offset shadow resolve to different clamp
  // rects). The graph shapes carry more (see AOTResourceProfile.h).
  static constexpr size_t MaxShaderTiledChainLeaves = ChainResourceCapacity.tiledRecipes;

  // The parameter blocks every shape shares: one chain-wide gradient block (and its SourceCoords
  // varying, shared with the noise source), one color-space transform block, two device-space and
  // two local rect coverage blocks, and four rrect array elements. They are the declared lengths of
  // the matching arrays in pointwise_fragment_uniforms.inc. ValidateSlots is the only place that
  // enforces them; the chain builder's planner and Make() both run it on the same instruction
  // array, so planning and construction cannot disagree.
  static constexpr size_t MaxGradientSlots = TiledGraphResourceCapacity.gradients;
  static constexpr size_t MaxColorSpaceXformSlots = 1;
  static constexpr size_t MaxDeviceRectCoverageSlots = 2;
  static constexpr size_t MaxLocalRectCoverageSlots = 2;
  static constexpr size_t MaxRRectCoverageSlots = 4;

  // Logical sampler children before padding. The artifacts bind 0 or 4 samplers; the builder
  // pads 1, 2 and 3 children to 4 without adding sampled DAG slots.
  static bool HasChainKernelVariant(size_t samplerChildren) {
    return samplerChildren <= static_cast<size_t>(MaxFusedAOTSamplers);
  }

  /**
   * The data-only half of Make(): validates an encoded instruction array (register domains,
   * opcodes, selector fields, leaf layout, per-kind parameter blocks) and resolves its resource
   * profile against the profile capacities, without looking at any child processor. The chain
   * builder runs it while planning (AOTPlanExecutor::CanExecute) and Make() runs it again on the
   * same data, so planning and construction cannot disagree on a slot rule or a budget.
   * leafCount is the number of real DAG texture leaves (slots [0, leafCount) must be OP_TEXTURE),
   * hasLUT whether a LUT gradient child follows them, and tiledLeafIndices the leaves carrying a
   * shader-side tiling recipe. On success *profile holds the resolved (non-Automatic) profile.
   */
  static bool ValidateSlots(const std::vector<AOTChainSlot>& slots, size_t rootSlot,
                            int coverageRootSlot, int clipCoverageRegister, size_t leafCount,
                            bool hasLUT, const std::vector<int>& tiledLeafIndices,
                            AOTResourceProfile* profile);

  static PlacementPtr<AOTPointwiseChainProcessor> Make(
      BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
      const std::vector<AOTChainSlot>& slots, size_t rootSlot, int tiledLeafIndex = -1,
      const AOTTiledTextureRecipe* tiledRecipe = nullptr,
      PlacementPtr<FragmentProcessor> maskChild = nullptr, int coverageRootSlot = -1,
      uint32_t coordSourceMask = ~0u, PlacementPtr<FragmentProcessor> lutChild = nullptr,
      int lutLeafIndex = -1, std::vector<PlacementPtr<FragmentProcessor>> samplerPadding = {},
      bool maskChildIsPhantom = false, int clipCoverageRegister = -1, int tiledLeafIndex2 = -1,
      const AOTTiledTextureRecipe* tiledRecipe2 = nullptr,
      AOTResourceProfile resourceProfile = AOTResourceProfile::Automatic,
      const std::vector<AOTChainTiledLeaf>& additionalTiledLeaves = {},
      const std::vector<AOTChainDeviceLeaf>& deviceLeaves = {});

  AOTPointwiseChainProcessor(std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
                             const std::vector<AOTChainSlot>& slots, size_t rootSlot,
                             int tiledLeafIndex, const AOTTiledTextureRecipe* tiledRecipe,
                             PlacementPtr<FragmentProcessor> maskChild, int coverageRootSlot,
                             uint32_t coordSourceMask, PlacementPtr<FragmentProcessor> lutChild,
                             int lutLeafIndex,
                             std::vector<PlacementPtr<FragmentProcessor>> samplerPadding,
                             bool maskChildIsPhantom, int clipCoverageRegister, int tiledLeafIndex2,
                             const AOTTiledTextureRecipe* tiledRecipe2,
                             AOTResourceProfile resourceProfile,
                             const std::vector<AOTChainTiledLeaf>& additionalTiledLeaves,
                             const std::vector<AOTChainDeviceLeaf>& deviceLeaves);

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

  bool hasNoise() const {
    return noiseSamplerBase >= 0;
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

  // Register index of the narrow clip slots' product (and the atlas path's glyph-mask subtree
  // root), or -1 when the chain has none. The value is a pure coverage that rides the XP's
  // coverage input — see the kernel's ClipCoverageRegister.
  int clipCoverage() const {
    return clipCoverageRegister;
  }

  // Index of the leaf that reads the first shader-side tiling recipe (wrap/border emulation), or
  // -1 when every leaf is plain.
  int tiledLeaf() const {
    return tiledLeaves.empty() ? -1 : tiledLeaves[0].leafIndex;
  }

  // Index of the leaf that reads the second tiling recipe, or -1 when at most one leaf is tiled.
  int tiledLeaf2() const {
    return tiledLeaves.size() < 2 ? -1 : tiledLeaves[1].leafIndex;
  }

  AOTResourceProfile resourceProfile() const {
    return _resourceProfile;
  }

  const std::vector<AOTChainTiledLeaf>& tiledLeafParameters() const {
    return tiledLeaves;
  }

  // Sampler index of the LUT gradient child, or -1 when the chain has none. The LUT child binds a
  // 2D texture into a leaf slot, so a chain carrying one cannot use a rectangle (sampler2DRect)
  // leaf variant.
  int lutLeaf() const {
    return lutLeafIndex;
  }

  // Bit k is set when leaf k samples in destination device space; zero when every leaf samples
  // through its coordinate transform.
  uint32_t deviceLeafMask() const;

  const AOTTiledTextureRecipe& tiledRecipe() const {
    static const AOTTiledTextureRecipe empty = {};
    return tiledLeaves.empty() ? empty : tiledLeaves[0].recipe;
  }

  const AOTTiledTextureRecipe& tiledRecipe2() const {
    static const AOTTiledTextureRecipe empty = {};
    return tiledLeaves.size() < 2 ? empty : tiledLeaves[1].recipe;
  }

  const AOTChainSlot& slot(size_t index) const {
    return slots[index];
  }

  void emitCode(EmitArgs& args) const override;

 protected:
 private:
  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;

  size_t _slotCount = 0;
  size_t rootSlot = 0;
  AOTResourceProfile _resourceProfile = AOTResourceProfile::Chain;
  std::vector<AOTChainTiledLeaf> tiledLeaves = {};
  std::array<AOTChainSlot, MaxSlots> slots = {};
  // Kept only to mark the mask's presence; the mask itself is the last registered child.
  // The mask sampler slot is occupied (real mask or phantom) — four-leaf chains always occupy it.
  bool hasMaskSlotChild = false;
  bool hasMaskChild = false;
  // Registered only by extended profiles, ahead of the leaf transforms.
  CoordTransform gradientCoordTransform = {};
  int coverageRootSlot = -1;
  // Register index of the narrow clip product / atlas coverage root; -1 when absent.
  int clipCoverageRegister = -1;
  // Per-target coordinate source for the chain vertex stage: bit k set sources leaf k from the
  // uvCoord attribute, bit 4 does the same for the procedural-source coordinates (SourceCoords);
  // clear bits source from
  // aPosition. All-ones reproduces the legacy shared-source behavior (quads, meshes); atlas text
  // sets only the atlas leaf's bit so gradients and image leaves come from position.
  uint32_t coordSourceMask = ~0u;
  // Sampler-only child for the LUT gradient colorizer, registered after the DAG leaves (and
  // counted by leafCount() so the matcher sizes TEXTURE_COUNT for it) but holding no chain slot;
  // lutLeafIndex is its sampler index, read by the kernel's OP_GRADIENT LUT branch.
  int lutLeafIndex = -1;
  int noiseSamplerBase = -1;
  std::vector<AOTChainDeviceLeaf> deviceLeaves = {};
};

}  // namespace tgfx
