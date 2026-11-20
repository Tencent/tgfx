/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/SamplingArgs.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/shaders/KernelContract.h"

namespace tgfx {

/// One sampler-binding child of a chain program, in artifact order: a DAG leaf, the LUT gradient
/// texture, phantom sampler padding (including the noise lookup tables), or the device mask. The
/// binding carries exactly the data the child TextureEffect was constructed from, so the uniform
/// upload reads the program instead of walking processor children.
struct AOTBinding {
  enum class Kind { Leaf, Lut, Padding, Mask };
  Kind kind = Kind::Leaf;
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  /// The child TextureEffect's sampling (Subset, AlphaOnly, RGBAAA derive from it). Mask ignores
  /// it and reads the matrix instead.
  SamplingArgs args = {};
  /// Mask only: the device-space sampling matrix (destination coordinates to the mask's space).
  Matrix uvMatrix = Matrix::I();
  /// The RGBAAA dual-plane alpha offset, as the child TextureEffect carries it; Point::Zero() for
  /// a single-plane source (matching TextureEffect::hasRGBAAA).
  Point alphaStart = Point::Zero();
};

/// The runtime operator of one chain slot. The integer values are part of the precompiled kernel
/// ABI: each member assigns its Op constant directly, and shader_build_tool injects the same
/// values into every compiled variant as the OP_* defines. The pointwise color operators generate
/// from the op list (TGFX_POINTWISE_OPS in KernelContract.h); the rest is the chain interpreter's
/// instruction vocabulary.
enum class AOTChainOp : int {
#define TGFX_AOT_CHAIN_OP(name, id, params, member, passBoundarySensitive) name = Op::name,
  TGFX_POINTWISE_OPS(TGFX_AOT_CHAIN_OP)
#undef TGFX_AOT_CHAIN_OP
      None = Op::None,
  Texture = Op::Texture,
  AARectCoverage = Op::AARectCoverage,
  Gradient = Op::Gradient,
  LocalRectCoverage = Op::LocalRectCoverage,
  RRectCoverage = Op::RRectCoverage,
  TexModulate = Op::TexModulate,
  InputOpaque = Op::InputOpaque,
  MulAlpha = Op::MulAlpha,
  PerlinNoise = Op::PerlinNoise,
};

/// One node of a pointwise DAG after flattening into the fused kernel's instruction array.
/// in0/in1 are result-register indices: -1 selects the geometry color (the Color uniform), -2
/// marks an unused input, -3 the coverage unit, -4 opaque white, -5 the geometry color with
/// opaque alpha. Only Blend uses both inputs. outRegister names the chainResults element the
/// instruction writes (-1: nothing ever reads the result); registers are recycled by
/// last-use (AOTChainRegisterAllocator), so the instruction count and the live-result count
/// are independent budgets.
struct AOTChainSlot {
  AOTChainOp op = AOTChainOp::None;
  int in0 = -2;
  int in1 = -2;
  // Result register this instruction writes into chainResults, or -1 when dead.
  int outRegister = -1;
  // OP_TEXTURE only: 1 modulates the sample by the geometry color alpha (color sources fed
  // directly from the paint color), 0 samples raw (blend operands such as coverage masks, which
  // the runtime emits without input-alpha modulation).
  int textureModulate = 0;
  // OP_TEXTURE only: 1 marks an alpha-only leaf (R8 on Metal, so the sampler returns (r,0,0,1)).
  // The kernel splats .r into all channels so the alpha channel carries the mask value, matching
  // the runtime TextureEffect readback. Uploaded as bit 1 of the slot selector; textureModulate
  // remains bit 0.
  int textureAlphaOnly = 0;
  // OP_TEXTURE only: 1 modulates an alpha-only leaf by the geometry color's RGB (bit 3 of the
  // selector). The runtime alpha-only readback is sample.a * inputColor, so the leaf's input
  // environment decides the shape: a two-child blend operand multiplies by (geom.rgb, 1.0)
  // because the xfer emission feeds each child vec4(inputColor.rgb, 1.0), while single-child
  // operands receive white and coverage masks stay raw.
  int textureModulateGeometryRGB = 0;
  // OP_TEXTURE only: 1 modulates an alpha-only leaf by the FULL geometry color (bit 4 of the
  // selector). The color root's runtime readback is sample.a * inputColor with the complete
  // (premultiplied) geometry color, so one multiply carries the RGB and the alpha together (a
  // bit0+bit3 pair would apply the paint alpha to the RGB twice).
  int textureModulateFullInput = 0;
  // OP_TEXTURE only: 1 modulates the sample by the coverage unit's alpha (bit 2 of the selector).
  // Set on a leaf that is the coverage subtree's root, matching the runtime coverage-FP readback
  // (tex * coverageIn.a).
  int textureModulateUnit = 0;
  // OP_TEX_MODULATE only: the leading raw-sampling texture slot this instruction reads (packed
  // into selector bits 16-19), and whether that leaf is alpha-only (selector bit 0 — the same
  // encoding the texture slot itself uses for bit 1, reused because the raw slot's own flags
  // are all clear).
  int texModulateSourceSlot = -1;
  int texModulateAlphaOnly = 0;
  // The pointwise color operators' parameter blocks, generated from the op list
  // (TGFX_POINTWISE_OPS in KernelContract.h) in the list's order, so the layout is stable.
#define TGFX_AOT_CHAIN_SLOT_MEMBER(name, id, params, member, passBoundarySensitive) \
  params member = {};
  TGFX_POINTWISE_OPS(TGFX_AOT_CHAIN_SLOT_MEMBER)
#undef TGFX_AOT_CHAIN_SLOT_MEMBER
  // OP_AARECT_COVERAGE only: the clip rect in device coordinates. The kernel reads the chain-wide
  // CoverageRect uniform, so at most one slot per chain may carry this op (enforced by the
  // builder).
  AOTRectCoverageParameters rectCoverage = {};
  // OP_LOCAL_RECT_COVERAGE only: the local-space clip rect. The kernel reads the chain-wide
  // CoverageLocalRect / CoverageLocalDeviceToLocal uniforms, so at most one slot per chain may
  // carry this op (enforced by the builder).
  AOTRectCoverageParameters localRectCoverage = {};
  // OP_RRECT_COVERAGE only: the rounded-rect parameters. The kernel reads chain-wide CoverageRRect*
  // uniform arrays indexed by the slot's rrect ordinal (assigned in slot order by the builder,
  // carried in the selector's bits 16-19), so at most four such slots per chain.
  AOTRRectCoverageParameters rrectCoverage = {};
  // OP_GRADIENT only: layout/colorizer parameters. The kernel reads chain-wide Gradient*
  // uniforms, so at most one slot per chain may carry this op (enforced by the builder).
  AOTGradientParameters gradient = {};
  AOTPerlinNoiseParameters noise = {};
  // OP_YUV only: the multi-plane source's own facts. The kernel reads them from the slot's data
  // record, so at most one such slot per chain is meaningless-by-construction (a chain has one
  // color root), enforced by the encoder.
  AOTYUVParameters yuv = {};
};

/// One chain leaf that samples with shader-side wrap/border emulation (see AOTTiledTextureRecipe).
struct AOTChainTiledLeaf {
  int leafIndex = -1;
  AOTTiledTextureRecipe recipe = {};
};

/// One chain leaf that samples in destination device space, as a DeviceSpaceTextureEffect does: its
/// coordinate is uvMatrix applied to the fragment position, not its TransformedCoords varying. The
/// leaf child itself is a plain TextureEffect over textureProxy with an unbounded subset, so the
/// kernel's subset clamp is a no-op for it. Every leaf of a chain may be one, each with its own
/// matrix.
struct AOTChainDeviceLeaf {
  int leafIndex = -1;
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  Matrix uvMatrix = Matrix::I();
};

/**
 * The compiled, data-only program of one chain pass: the register-allocated instruction array, the
 * tiled and device-space leaf records, and the resolved resource profile — everything the kernel
 * reads besides the sampler children. AOTChainBuilder::Compile produces it as the single admission
 * entry (plan-time fit checks and construction run exactly this pipeline); the shell-free serving
 * path consumes it directly, and fixed-program matching keys on its canonical shape.
 */
/// The selector word a chain slot's packed record carries: the op-specific small value in the low
/// bits and the result register + 1 in bits 20-24 (zero marks a dead instruction). One definition
/// shared by the runtime binder and the specialization values, so the two cannot drift.
inline int ChainSlotSelector(const AOTChainSlot& slot) {
  int selector = slot.op == AOTChainOp::Blend
                     ? slot.blend.blendMode | (slot.blend.multiplyInputAlpha ? 0x100 : 0)
                     : 0;
  if (slot.op == AOTChainOp::ConstColor) {
    selector = slot.constColor.inputMode;
  }
  if (slot.op == AOTChainOp::Texture) {
    // Bit 0: modulate by the paint alpha. Bit 1: alpha-only leaf, splat .r into all channels.
    // Bit 2: coverage-root leaf, modulate by the coverage unit alpha. Bit 3: alpha-only leaf
    // modulated by the geometry color's RGB. Bit 4: alpha-only leaf modulated by the full
    // geometry color.
    selector = slot.textureModulate | (slot.textureAlphaOnly << 1) |
               (slot.textureModulateUnit << 2) | (slot.textureModulateGeometryRGB << 3) |
               (slot.textureModulateFullInput << 4);
  }
  if (slot.op == AOTChainOp::TexModulate) {
    // Bit 0: the source leaf is alpha-only. Bits 16-19: the leading raw-sampling slot's index.
    selector = slot.texModulateAlphaOnly | (slot.texModulateSourceSlot << 16);
  }
  selector |= (slot.outRegister + 1) << 20;
  return selector;
}

struct AOTProgram {
  std::vector<AOTChainSlot> slots = {};
  std::vector<AOTChainTiledLeaf> tiledLeaves = {};
  std::vector<AOTChainDeviceLeaf> deviceLeaves = {};
  /// The sampler-binding children in artifact order (leaves, LUT, padding, mask).
  std::vector<AOTBinding> bindings = {};
  // Bit k is set when leaf k samples in destination device space; the coord-source mask for the
  // chain vertex stage (bit k sources leaf k from the uvCoord attribute, bit 4 the procedural
  // sources).
  uint32_t coordSourceMask = ~0u;
  size_t rootRegister = 0;
  int coverageRootRegister = -1;
  int clipCoverageRegister = -1;
  // Sampler index of the LUT gradient child, or -1 when the chain has none.
  int lutLeafIndex = -1;
  /// The sampler children the TEXTURE_COUNT variant dimension is encoded from: the DAG leaves, the
  /// LUT gradient texture, the noise lookup tables and the phantom sampler padding — every binding
  /// except the mask slot child. The artifacts exist only as 0- and 4-sampler forms, so this is 0
  /// or 4 (the builder pads 1-3 children up to four).
  uint32_t samplerChildCount = 0;
  /// Whether the mask binding is a real device mask (a phantom mask slot reads as absent).
  bool hasMaskTexture = false;
  /// Sampler index of the noise lookup tables' first binding, or -1 when the chain has none.
  int noiseSamplerBase = -1;
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
};

/// What one chain program's slots carry, as the bit sets the text gates read through scalar spec
/// constants: a gate such as `(SPEC_UsedOps() & (1 << OP_PERLIN_NOISE)) != 0` cuts the noise
/// reader out of every variant whose draw never reaches it. The masks are a function of the
/// program alone, so the specialization values and the generic form's uniform upload derive them
/// from this one place instead of each walking the slots.
struct AOTChainFeatureMasks {
  /// Bit `Op::<name>` set for every op a slot carries.
  uint32_t usedOps = 0;
  /// Bit `BlendMode` set for every Blend slot's mode (the enum has 29 values, within 32 bits).
  uint32_t blendModes = 0;
  /// Bit 0 an alpha-only leaf, bit 1 a shader-side tiled recipe, bit 2 a destination-space leaf.
  uint32_t textureFlags = 0;
  /// Bits 0-3 the gradient colorizer kind, bits 8-11 the layout type: the two nibbles the
  /// gradient reader branches on.
  uint32_t gradientKinds = 0;
};

/// Whether the chain fragment needs the shared blend-mode switch: a Blend slot executes it for the
/// slot's mode, and a shader-side xfer with an advanced mode runs it in the draw's blend pass.
/// Both callers (the specialization values and the generic form's uniform upload) derive the flag
/// from this one rule, so the specialized and generic texts gate identically.
/// The blend mode where the shader-side (XP) implementation leaves the coefficient arms and enters
/// the advanced group. Must equal XP_FIRST_ADVANCED_MODE in xp/xp_blend_colors.inc: the GLSL switch
/// and this predicate decide the same thing, so a divergence silently runs the wrong arm (or skips
/// the blend pass entirely). The static_assert pins the enum half of that pair.
inline constexpr int XpFirstAdvancedMode = 15;
static_assert(static_cast<int>(BlendMode::Overlay) == XpFirstAdvancedMode,
              "XP_FIRST_ADVANCED_MODE (xp_blend_colors.inc) must track BlendMode::Overlay");

/// Whether an advanced shader-side xfer mode applies: it runs the blend pass even when no slot
/// carries a Blend op. One definition for the two writers that ask.
inline bool XpNeedsAdvancedMath(bool shaderBlend, int blendMode) {
  return shaderBlend && blendMode >= XpFirstAdvancedMode;
}

inline bool ChainNeedsBlendMath(const AOTProgram& program, bool xpAdvancedMode) {
  if (xpAdvancedMode) {
    return true;
  }
  for (const auto& slot : program.slots) {
    if (slot.op == AOTChainOp::Blend) {
      return true;
    }
  }
  return false;
}

inline AOTChainFeatureMasks ChainFeatureMasksOf(const AOTProgram& program) {
  constexpr uint32_t kAlphaOnlyLeaf = 0x1;
  constexpr uint32_t kShaderTiledLeaf = 0x2;
  constexpr uint32_t kDeviceSpaceLeaf = 0x4;
  AOTChainFeatureMasks masks = {};
  for (const auto& slot : program.slots) {
    masks.usedOps |= 1u << static_cast<uint32_t>(slot.op);
    if (slot.op == AOTChainOp::Blend) {
      masks.blendModes |= 1u << static_cast<uint32_t>(slot.blend.blendMode);
    }
    if (slot.op == AOTChainOp::Texture && slot.textureAlphaOnly != 0) {
      masks.textureFlags |= kAlphaOnlyLeaf;
    }
    if (slot.op == AOTChainOp::Gradient) {
      masks.gradientKinds |= 1u << (static_cast<uint32_t>(slot.gradient.colorizerKind) & 0x0Fu);
      masks.gradientKinds |= 1u << (8u + (static_cast<uint32_t>(slot.gradient.layoutType) & 0x0Fu));
    }
  }
  // The tiled and device-space facts live on the program's leaf records, not on the slots: only
  // the leaves that need shader-side wrap emulation or a destination-space matrix appear there.
  if (!program.tiledLeaves.empty()) {
    masks.textureFlags |= kShaderTiledLeaf;
  }
  if (!program.deviceLeaves.empty()) {
    masks.textureFlags |= kDeviceSpaceLeaf;
  }
  return masks;
}

}  // namespace tgfx
