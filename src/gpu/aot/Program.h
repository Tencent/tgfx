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
#include "gpu/AOTResourceProfile.h"
#include "gpu/SamplingArgs.h"
#include "gpu/aot/EffectGraph.h"
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
/// ABI: they must match the OP_* constants in operator/op_codes.inc, which shader_build_tool
/// validates against the shared Op constants at bundle build time.
enum class AOTChainOp : int {
  ColorMatrix = Op::ColorMatrix,
  Luma = Op::Luma,
  AlphaThreshold = Op::AlphaThreshold,
  ColorSpaceXform = Op::ColorSpaceXform,
  None = Op::None,
  Texture = Op::Texture,
  ConstColor = Op::ConstColor,
  Blend = Op::Blend,
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
  // (premultiplied) geometry color, so one multiply carries the RGB and the alpha together;
  // the former bit0+bit3 combination applied the paint alpha to the RGB twice.
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
  // OP_RRECT_COVERAGE only: the rounded-rect parameters. The kernel reads chain-wide CoverageRRect*
  // uniform arrays indexed by the slot's rrect ordinal (assigned in slot order by the builder,
  // carried in the selector's bits 16-19), so at most four such slots per chain.
  AOTRRectCoverageParameters rrectCoverage = {};
  // OP_GRADIENT only: layout/colorizer parameters. The kernel reads chain-wide Gradient*
  // uniforms, so at most one slot per chain may carry this op (enforced by the builder).
  AOTGradientParameters gradient = {};
  AOTPerlinNoiseParameters noise = {};
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

}  // namespace tgfx
