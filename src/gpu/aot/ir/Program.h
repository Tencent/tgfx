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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
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
  /// A shader-tiled leaf's recipe: the assembled text maps its coordinate through these tiling
  /// facts (the interpreter carried them as the Tiled* uniforms).
  std::optional<TiledTextureSampling> tiledRecipe = std::nullopt;
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
 * The compiled, data-only program of one chain pass: the sampler-binding table the upload walks,
 * the device-space leaf records, and the resolved resource profile — everything the kernel's
 * writers read besides the sampler children. BuildChainProgram produces it straight from the
 * graph layout (the slot instruction encoding it used to carry is retired: the assembled text
 * spells the draw's operators directly and every parameter reader addresses its graph node).
 */
struct AOTProgram {
  std::vector<AOTBinding> bindings = {};
  std::vector<AOTChainDeviceLeaf> deviceLeaves = {};
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

/// The blend mode where the shader-side (XP) implementation leaves the coefficient arms and enters
/// the advanced group. Must equal XP_FIRST_ADVANCED_MODE in xp/xp_blend_colors.inc: the GLSL switch
/// and this predicate decide the same thing, so a divergence silently runs the wrong arm (or skips
/// the blend pass entirely). The static_assert pins the enum half of that pair.
inline constexpr int XpFirstAdvancedMode = 15;
static_assert(static_cast<int>(BlendMode::Overlay) == XpFirstAdvancedMode,
              "XP_FIRST_ADVANCED_MODE (xp_blend_colors.inc) must track BlendMode::Overlay");

/// Whether an advanced shader-side xfer mode applies: it runs the blend pass even when no operator
/// carries a Blend op. One definition for the writers that ask.
inline bool XpNeedsAdvancedMath(bool shaderBlend, int blendMode) {
  return shaderBlend && blendMode >= XpFirstAdvancedMode;
}

}  // namespace tgfx
