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

#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/Swizzle.h"
#include "gpu/aot/Binder.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/resources/TextureView.h"
#include "gpu/shaders/modules/ChainParams.h"

namespace tgfx {

bool AOTPointwiseChainProcessor::ValidateSlots(const std::vector<AOTChainSlot>& slots,
                                               size_t rootSlot, int coverageRootSlot,
                                               int clipCoverageRegister, size_t leafCount,
                                               bool hasLUT,
                                               const std::vector<int>& tiledLeafIndices,
                                               AOTResourceProfile* profile) {
  if (profile == nullptr || slots.empty() || slots.size() > MaxSlots) {
    return false;
  }
  // Validation domains are separated (fail-closed): in0/in1 are INPUT selectors and
  // may carry the kernel's special negative designators (-1 geometry color ... -5 opaque
  // geometry color) or a result register; outRegister / rootSlot / coverageRootSlot /
  // clipCoverageRegister are OUTPUT registers and must name a real chainResults slot (>= 0) or
  // the explicit dead-instruction marker -1 (a raw sampling slot whose result nothing reads —
  // the computed-input texture's leading fetch). The upload packs the output as
  // (outRegister + 1) << 20, so any value below -1 would shift left into undefined behavior
  // instead of being rejected here.
  const auto validInput = [](int reg) { return reg >= -5 && reg < static_cast<int>(MaxRegisters); };
  const auto validOutput = [](int reg) {
    return reg >= -1 && reg < static_cast<int>(MaxRegisters);
  };
  if (rootSlot >= MaxRegisters || (coverageRootSlot >= 0 && !validOutput(coverageRootSlot)) ||
      clipCoverageRegister >= static_cast<int>(MaxRegisters) || leafCount > slots.size()) {
    return false;
  }
  for (const auto& slot : slots) {
    if (!validOutput(slot.outRegister) || !validInput(slot.in0) || !validInput(slot.in1)) {
      return false;
    }
    // Opcode whitelist: the kernel's evaluation switch has no default branch (an unknown value
    // falls through into the ColorSpaceXform evaluation), so an out-of-enum opcode must be
    // rejected here rather than silently misinterpreted.
    switch (slot.op) {
      case AOTChainOp::ColorMatrix:
      case AOTChainOp::Luma:
      case AOTChainOp::AlphaThreshold:
      case AOTChainOp::ColorSpaceXform:
      case AOTChainOp::Texture:
      case AOTChainOp::ConstColor:
      case AOTChainOp::Blend:
      case AOTChainOp::AARectCoverage:
      case AOTChainOp::Gradient:
      case AOTChainOp::LocalRectCoverage:
      case AOTChainOp::RRectCoverage:
      case AOTChainOp::TexModulate:
      case AOTChainOp::InputOpaque:
      case AOTChainOp::MulAlpha:
      case AOTChainOp::PerlinNoise:
        break;
      case AOTChainOp::None:
      default:
        return false;
    }
    // Selector sub-field domains: the upload packs these ints straight into the selector's bit
    // fields, so an out-of-domain value does not merely misbehave — it bleeds into neighboring
    // bits (the result register rides bits 20-24, the rrect/tex-source ordinals bits 16-19) and
    // can index chainResults/chainLeafTex out of bounds in the kernel. The owned API is public
    // surface, so fail closed here instead of trusting the builder.
    if (slot.op == AOTChainOp::ConstColor &&
        (slot.constColor.inputMode < 0 || slot.constColor.inputMode > 2)) {
      return false;
    }
    if (slot.op == AOTChainOp::Blend && (slot.blend.blendMode < 0 || slot.blend.blendMode > 0xFF ||
                                         slot.blend.childType < 0 || slot.blend.childType > 2)) {
      return false;
    }
    if (slot.op == AOTChainOp::Texture &&
        ((slot.textureModulate | slot.textureAlphaOnly | slot.textureModulateGeometryRGB |
          slot.textureModulateFullInput | slot.textureModulateUnit) &
         ~1) != 0) {
      return false;
    }
    if (slot.op == AOTChainOp::TexModulate &&
        (slot.texModulateSourceSlot < 0 || slot.texModulateSourceSlot > 3 ||
         (slot.texModulateAlphaOnly & ~1) != 0)) {
      return false;
    }
    // Required-input checks: the kernel resolves an unmapped input (-2) to vec4(0.0) silently, so
    // an operator that actually consumes its input must not leave it unmapped. Texture slots
    // carry flags in in0/in1 rather than register indices, and Texture is exempt.
    if (slot.op != AOTChainOp::Texture && slot.op != AOTChainOp::PerlinNoise && slot.in0 == -2) {
      return false;
    }
    if ((slot.op == AOTChainOp::Blend || slot.op == AOTChainOp::MulAlpha) && slot.in1 == -2) {
      return false;
    }
  }
  for (size_t index = 0; index < tiledLeafIndices.size(); ++index) {
    auto leafIndex = tiledLeafIndices[index];
    if (leafIndex < 0 || static_cast<size_t>(leafIndex) >= leafCount) {
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (tiledLeafIndices[previous] == leafIndex) {
        return false;
      }
    }
  }
  // Only real DAG leaves have prefetched samples; LUT and padding children do not.
  for (size_t index = 0; index < slots.size(); ++index) {
    const auto& slot = slots[index];
    if ((slot.op == AOTChainOp::Texture) != (index < leafCount)) {
      return false;
    }
    if (slot.op == AOTChainOp::TexModulate &&
        static_cast<size_t>(slot.texModulateSourceSlot) >= leafCount) {
      return false;
    }
  }
  size_t localRectSlots = 0;
  size_t deviceRectSlots = 0;
  size_t colorSpaceSlots = 0;
  size_t gradientSlots = 0;
  size_t noiseSlots = 0;
  size_t rrectSlots = 0;
  for (const auto& slot : slots) {
    if (slot.op == AOTChainOp::ColorSpaceXform &&
        (!ColorSpaceXformStepsSupported(slot.colorSpaceXform.steps.get()) ||
         ++colorSpaceSlots > MaxColorSpaceXformSlots)) {
      return false;
    }
    if (slot.op == AOTChainOp::PerlinNoise &&
        (++noiseSlots > 1 || !AOTPerlinNoiseParametersSupported(slot.noise) ||
         slot.noise.permutationsView == nullptr || slot.noise.noiseView == nullptr ||
         leafCount > 2 || hasLUT)) {
      return false;
    }
    if (slot.op == AOTChainOp::Gradient && ++gradientSlots > MaxGradientSlots) {
      return false;
    }
    if (slot.op == AOTChainOp::AARectCoverage && ++deviceRectSlots > MaxDeviceRectCoverageSlots) {
      return false;
    }
    // The kernel carries two local-rect parameter sets and four rrect array elements.
    if (slot.op == AOTChainOp::LocalRectCoverage && ++localRectSlots > MaxLocalRectCoverageSlots) {
      return false;
    }
    if (slot.op == AOTChainOp::RRectCoverage && ++rrectSlots > MaxRRectCoverageSlots) {
      return false;
    }
  }
  if (noiseSlots > 0 && gradientSlots > 0) {
    return false;
  }
  auto resolved = *profile;
  if (resolved == AOTResourceProfile::Automatic) {
    resolved = noiseSlots > 0      ? AOTResourceProfile::NoiseGraph
               : gradientSlots > 0 ? AOTResourceProfile::TiledGraph
                                   : AOTResourceProfile::Chain;
  }
  AOTResourceDemand demand = {};
  demand.instructions = slots.size();
  demand.leafBindings = leafCount + (hasLUT ? 1u : 0u) + noiseSlots * 2;
  demand.tiledRecipes = tiledLeafIndices.size();
  demand.noiseSources = noiseSlots;
  demand.gradients = gradientSlots;
  if (!FitsAOTResourceProfile(demand, resolved)) {
    return false;
  }
  *profile = resolved;
  return true;
}

PlacementPtr<AOTPointwiseChainProcessor> AOTPointwiseChainProcessor::Make(
    BlockAllocator* allocator, AOTProgram program, AOTChainChildren children) {
  if (allocator == nullptr) {
    return nullptr;
  }
  auto leafCount = children.leaves.size();
  std::vector<int> tiledLeafIndices = {};
  for (const auto& tiled : program.tiledLeaves) {
    tiledLeafIndices.push_back(tiled.leafIndex);
  }
  AOTResourceProfile profile = program.profile;
  if (!ValidateSlots(program.slots, program.rootRegister, program.coverageRootRegister,
                     program.clipCoverageRegister, leafCount, children.lut != nullptr,
                     tiledLeafIndices, &profile)) {
    return nullptr;
  }
  program.profile = profile;
  for (const auto& leaf : children.leaves) {
    if (leaf == nullptr) {
      return nullptr;
    }
  }
  for (const auto& child : children.padding) {
    if (child == nullptr || !IsProcessor<TextureEffect>(child.get()) ||
        child->numTextureSamplers() > 1) {
      return nullptr;
    }
  }
  // Sampler-binding children include the DAG leaves plus sampler-only children (the LUT gradient
  // texture and phantom padding). TEXTURE_COUNT exists only as 0 or 4, so any chain with leaves
  // must pad exactly up to the four-sampler artifacts.
  size_t samplerChildren = leafCount + (children.lut != nullptr ? 1 : 0) + children.padding.size();
  if (samplerChildren != 0 && samplerChildren != 4) {
    return nullptr;
  }
  if (children.mask != nullptr && !children.maskIsPhantom &&
      !IsProcessor<DeviceSpaceTextureEffect>(children.mask.get())) {
    return nullptr;
  }
  if (children.lut != nullptr && (program.lutLeafIndex < 0 || program.lutLeafIndex > 3 ||
                                  static_cast<size_t>(program.lutLeafIndex) != leafCount)) {
    return nullptr;
  }
  for (const auto& slot : program.slots) {
    if (slot.op != AOTChainOp::PerlinNoise) {
      continue;
    }
    // The two noise lookup tables ride the first two sampler-only children.
    if (children.padding.size() < 2 || children.padding[0] == nullptr ||
        children.padding[1] == nullptr ||
        children.padding[0]->textureAt(0) != slot.noise.permutationsView->getTexture() ||
        children.padding[1]->textureAt(0) != slot.noise.noiseView->getTexture()) {
      return nullptr;
    }
  }
  // A device-space leaf is one of the DAG leaves (a texture slot), never a sampler-only child, and
  // each leaf takes at most one device-space matrix.
  uint32_t seenDeviceLeaves = 0;
  for (const auto& deviceLeaf : program.deviceLeaves) {
    if (deviceLeaf.leafIndex < 0 || static_cast<size_t>(deviceLeaf.leafIndex) >= leafCount ||
        deviceLeaf.textureProxy == nullptr || deviceLeaf.uvMatrix.hasPerspective()) {
      return nullptr;
    }
    auto bit = 1u << static_cast<uint32_t>(deviceLeaf.leafIndex);
    if ((seenDeviceLeaves & bit) != 0) {
      return nullptr;
    }
    seenDeviceLeaves |= bit;
  }
  return allocator->make<AOTPointwiseChainProcessor>(std::move(program), std::move(children));
}

AOTPointwiseChainProcessor::AOTPointwiseChainProcessor(AOTProgram program,
                                                       AOTChainChildren children)
    : FragmentProcessor(ClassID()), _program(std::move(program)),
      hasMaskSlotChild(children.mask != nullptr),
      hasMaskChild(children.mask != nullptr && !children.maskIsPhantom) {
  for (size_t index = 0; index < _program.slots.size(); ++index) {
    if (_program.slots[index].op == AOTChainOp::Gradient) {
      gradientCoordTransform = CoordTransform(_program.slots[index].gradient.coordMatrix);
    } else if (_program.slots[index].op == AOTChainOp::PerlinNoise) {
      gradientCoordTransform = CoordTransform(_program.slots[index].noise.uvMatrix);
      noiseSamplerBase = static_cast<int>(children.leaves.size());
    }
  }
  if (IsAOTGraphProfile(_program.profile)) {
    addCoordTransform(&gradientCoordTransform);
  }
  for (auto& leaf : children.leaves) {
    registerChildProcessor(std::move(leaf));
  }
  if (children.lut != nullptr) {
    registerChildProcessor(std::move(children.lut));
  }
  for (auto& phantom : children.padding) {
    registerChildProcessor(std::move(phantom));
  }
  if (children.mask != nullptr) {
    registerChildProcessor(std::move(children.mask));
  }
}

uint32_t AOTPointwiseChainProcessor::deviceLeafMask() const {
  uint32_t mask = 0;
  for (const auto& deviceLeaf : _program.deviceLeaves) {
    mask |= 1u << static_cast<uint32_t>(deviceLeaf.leafIndex);
  }
  return mask;
}

void AOTPointwiseChainProcessor::emitCode(EmitArgs& args) const {
  // This processor is only ever prepared through the decomposition executor's plan passes; a miss
  // makes the executor re-run the original draw, so the runtime program builder never sees it.
  // Emitting the DAG for JIT would require porting the blend emission of
  // XfermodeFragmentProcessor; until a caller needs it, fail loudly instead of silently producing
  // wrong pixels.
  LOGE("AOTPointwiseChainProcessor::emitCode() is not supported on the runtime path!");
  args.fragBuilder->codeAppendf("%s = vec4(1.0, 0.0, 1.0, 1.0);", args.outputColor.c_str());
}

void AOTPointwiseChainProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // The chain's whole instruction structure — slot count, wiring, opcodes, root, coverage root,
  // coord-source mask, LUT leaf — travels in per-draw uniforms that onSetData rewrites before
  // every draw, and the base class already keys this processor by class ID plus the children's
  // keys (texture identities, sampler flags), so the variant selection is preserved. Two chains
  // sharing a variant and a texture set therefore share one program and the per-draw data
  // differentiates them; encoding the instruction structure here would split the program cache
  // by effect topology and defeat the variant reuse that is the point of the chain kernel.
  static constexpr uint32_t ChainProgramTag = 0xA07C41A7u;
  bytesKey->write(ChainProgramTag);
  bytesKey->write(static_cast<uint32_t>(_program.profile));
}

void AOTPointwiseChainProcessor::onSetData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData) const {
  BindChainProgram(_program, vertexUniformData, fragmentUniformData);
}

}  // namespace tgfx
