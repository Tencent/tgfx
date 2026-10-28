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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <vector>
#include "gpu/AOTResourceProfile.h"
#include "gpu/shaders/ShaderFamily.h"

namespace tgfx {

class ProgramInfo;

/// Precompiled shader that evaluates an arbitrary pointwise DAG (texture and const-color leaves
/// combined by color-matrix, luma, alpha-threshold, color-space-xform and blend ops) in a single
/// fused pass. The DAG shape is runtime data: every node occupies one of 16 instruction slots
/// whose OpType and two input-register indices are uniforms, and the intermediates live in a
/// 16-entry register file recycled by last use, so any topology hits the same variant. Only the
/// texture-leaf count is a compile-time dimension, because each leaf adds a sampler binding and a
/// TransformedCoords varying.
///
/// The merged single pointwise family: RESOURCE_KIND != 0 selects the extended resource profiles
/// that were the pre-merge PointwiseGraphShader — kind 1 the gradient source with three tiled
/// recipes (AOTResourceProfile::TiledGraph), kind 2 the Perlin noise source (NoiseGraph). Their
/// reachable sets derive from the plain chain's (no per-quad subset; the noise kind keeps its
/// fixed common-color AA quad subset), the extended vertex form carries SourceCoords, and a match
/// the extended-kinds rule cannot place is reported as PermutationNotEnumerated, as it was on the
/// Graph family. Changing a plain-chain dimension therefore changes the extended kinds too.
///
/// Vertex dimensions:
///   GP_LAYOUT (int, 5 values): 0 = rect family (Default/Quad/Mesh/AtlasText), 1 = ellipse,
///     2 = hairline line (per-pixel edge coverage from the edge-distance varying),
///     3 / 4 = complex rounded rect fill / stroke (ComplexEllipseGP: per-corner radii; the stroke
///     form carries an extra stroke-width attribute, so it is its own layout).
///   HAS_COVERAGE (bool): per-vertex AA coverage varying present.
///   HAS_UV_COORD (bool): quad only; leaf coords come from the uvCoord attribute instead of
///     aPosition.
///   The color attribute is unconditional on every layout (a white filler where the buffer is
///     shared across draws): the exact common color rides the Color uniform through the
///     HasCommonColor runtime uniform, so there is no HAS_COLOR dimension.
///   There is no GP_TYPE: the position always goes through the Matrix uniform, which DefaultGP
///     fills with the view matrix and QuadPerEdgeAAGP fills with identity (bit-exact).
///   TEXTURE_COUNT (int, 2 values): 0 -> no texture leaves, 1 -> the four-leaf slots (chains
///     with 1-3 sampler children bind phantom padding and run them). A zero-leaf chain
///     evaluates const-color/blend ops against the geometry color alone.
///   HAS_SUBSET (bool): quad four-leaf chains only; the per-quad texSubset attribute of a
///     SrcRectConstraint::Strict draw, transformed into leaf 0's texture space and passed on as
///     the vTexSubset varying (the runtime QuadPerEdgeAAGeometryProcessor's subset varying).
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
///   HAS_COVERAGE / TEXTURE_COUNT: mirrored with the vertex stage.
///   HAS_MASK_TEXTURE (bool): a device-space alpha mask child sampled after the DAG and before the
///     XP stage, matching the legacy blend kernel's mask application point. Leaf-free uniform-color
///     chains on the DefaultGP, hairline-line and complex rounded-rect layouts only.
///   HAS_SUBSET: mirrored with the vertex stage; leaf 0 clamps to vTexSubset ahead of its Subset
///     uniform, as the runtime TextureEffect applies the GP subset of a strict draw.
class PointwiseChainShader : public ShaderFamily<PointwiseChainShader> {
 public:
  static constexpr const char* Name = "PointwiseChainShader";
  static constexpr const char* VertFile = "kernels/pointwise_chain.vert";
  static constexpr const char* FragFile = "kernels/pointwise_chain.frag";

  // The kernel implements the GRAY_8 / RG_88 runtime read swizzle through its TextureEffect
  // leaves, so the matcher may route such texture reads here.
  static constexpr uint32_t TextureReads = ShaderTextureReadSwizzle;

  struct VertDims {
    // The dimension declarations: each handle is one dimension (typed by this stage), domain()
    // lists the handles in the bundle's dimension order, and Compose assigns through the handles
    // by name (ComposedDims) — there is no index enum whose positions could drift from the
    // domain list.
    static inline const PermutationIntDim<VertDims> GP_LAYOUT{"GP_LAYOUT", 5};
    static inline const PermutationBoolDim<VertDims> HAS_COVERAGE{"HAS_COVERAGE"};
    static inline const PermutationBoolDim<VertDims> HAS_UV_COORD{"HAS_UV_COORD"};
    // 0 = no texture leaves, 1 = the four-leaf artifacts (chains with 1-3 sampler children bind
    // phantom padding and run them).
    static inline const PermutationIntDim<VertDims> TEXTURE_COUNT{"TEXTURE_COUNT", 2};
    // Appended so every subset-free index keeps its value.
    static inline const PermutationBoolDim<VertDims> HAS_SUBSET{"HAS_SUBSET"};
    // The resource kinds (RESOURCE_KIND != 0) carry the SourceCoords varying and reserve
    // CoordTransformMatrix_0 for the source module. Mirrored from the fragment tail; plain
    // chains keep their pre-merge indices.
    static inline const PermutationBoolDim<VertDims> HAS_SOURCE_COORDS{"HAS_SOURCE_COORDS"};
    static PermutationDomain domain() {
      return PermutationDomain(
          {GP_LAYOUT, HAS_COVERAGE, HAS_UV_COORD, TEXTURE_COUNT, HAS_SUBSET, HAS_SOURCE_COORDS});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // Same declaration shape as VertDims: handles typed by this stage, domain() in bundle order.
    // RESOURCE_KIND stays last (see its comment) — the order feeds encode(), so it is pinned by
    // the compiled-space tests, not by an enum.
    static inline const PermutationIntDim<FragDims> GP_LAYOUT{"GP_LAYOUT", 5};
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static inline const PermutationBoolDim<FragDims> HAS_COVERAGE{"HAS_COVERAGE"};
    static inline const PermutationIntDim<FragDims> TEXTURE_COUNT{"TEXTURE_COUNT", 2};
    static inline const PermutationBoolDim<FragDims> HAS_MASK_TEXTURE{"HAS_MASK_TEXTURE"};
    static inline const PermutationBoolDim<FragDims> HAS_SUBSET{"HAS_SUBSET"};
    // 0 = plain chain (16 slots), 1 = gradient source (16 slots), 2 = noise source (32 slots).
    // Appended last so plain chains keep their pre-merge indices; the noise kind holds the
    // largest value so a future removal re-encodes nothing below it.
    static inline const PermutationIntDim<FragDims> RESOURCE_KIND{"RESOURCE_KIND", 3};
    static PermutationDomain domain() {
      return PermutationDomain({GP_LAYOUT, HAS_XP, HAS_COVERAGE, TEXTURE_COUNT, HAS_MASK_TEXTURE,
                                HAS_SUBSET, RESOURCE_KIND});
    }
  };
  using FD = FragDims;

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = true;

  /// Shared input contract for the plain chains and the extended resource kinds. gpKind selects
  /// (0 = DefaultGP, 1 = QuadPerEdgeAA, 2 = Mesh, 3 = sampler-free AtlasText twin, 4 = Ellipse,
  /// 5 = HairlineLine, 6 = ComplexEllipse fill, 7 = ComplexEllipse stroke); each profile fixes
  /// which attribute slots its vertex buffers carry, and Compose rejects profile-inconsistent
  /// combinations. The chain slot topology (ops, indices) is runtime data; only the leaf-count
  /// class, the mask flag, the coverage-subtree flag, and the transfer type shape the shared
  /// dimensions. TiledGraph adds a resource-kind value for its single gradient and three tiled
  /// recipes; NoiseGraph retains its fixed position-based, common-color AA quad subset.
  struct Inputs {
    int gpKind = 0;
    bool hasCoverage = false;         // DefaultGP: AA coverage; quad/mesh: always true.
    bool hasUVCoord = false;          // quad/mesh: texCoord slot; atlas: always true.
    int textureCount = 0;             // domain value: 0 = leaf-free, 1 = four-leaf padded slots.
    bool hasMask = false;             // device-space mask child present.
    bool hasCoverageSubtree = false;  // chain coverage subtree present.
    bool hasSubset = false;           // quad four-leaf chain: per-quad subset of a strict draw.
    bool hasNoise = false;
    bool hasGradient = false;  // one gradient belongs to the extended TiledGraph profile.
    AOTResourceProfile resourceProfile = AOTResourceProfile::Automatic;
    int xpType = XP_UNSUPPORTED;
  };

  /// The extended resource kinds (RESOURCE_KIND != 0): the per-quad subset of a strict image draw
  /// is a plain-chain capability; the extended kernels have no subset branch.
  static bool ExtendedValuesAllowed(const ComposedDims<FragDims>& chainFrag) {
    return chainFrag.get(FD::HAS_SUBSET) == 0;
  }

  /// Pure mapping from inputs to dimension values; unsupported combinations return nullopt.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    auto profile = inputs.resourceProfile;
    // The plan layer resolves the profile before matching (an Automatic profile never auto-promotes,
    // as it never did on the pre-merge Graph family's match path): a graph profile selects the
    // extended resource kind, everything else is a plain chain.
    int resourceKind = 0;
    if (profile == AOTResourceProfile::NoiseGraph) {
      resourceKind = 2;
    } else if (profile == AOTResourceProfile::TiledGraph) {
      resourceKind = 1;
    } else if (inputs.hasNoise || inputs.hasGradient ||
               (profile != AOTResourceProfile::Automatic && profile != AOTResourceProfile::Chain)) {
      return std::nullopt;
    }
    if (resourceKind == 2 &&
        (!inputs.hasNoise || inputs.hasGradient || inputs.gpKind != 1 || !inputs.hasCoverage ||
         inputs.hasUVCoord || inputs.textureCount != 1)) {
      return std::nullopt;
    }
    if (resourceKind == 1 && inputs.hasNoise) {
      return std::nullopt;
    }
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    if (!ComposeGP(inputs, &vert, &frag)) {
      return std::nullopt;
    }
    if (resourceKind != 0 && !ExtendedValuesAllowed(frag)) {
      return std::nullopt;
    }
    // Both writes are unconditional — including the plain-chain zeros — because finish() fails
    // on any dimension left unassigned; a forgotten dimension can no longer read as a silent 0.
    vert.set(VD::HAS_SOURCE_COORDS, resourceKind != 0 ? 1 : 0);
    frag.set(FD::RESOURCE_KIND, resourceKind);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// The enumerable input space the Reachable set maps through Compose: the plain lattice, the
  /// same lattice under the TiledGraph profile (which Compose restricts to the extended
  /// subset-free shapes), and the NoiseGraph fixed common-color AA quad subset.
  static std::vector<Inputs> AllInputs() {
    std::vector<Inputs> result;
    const AOTResourceProfile profiles[] = {AOTResourceProfile::Automatic,
                                           AOTResourceProfile::TiledGraph};
    for (auto profile : profiles) {
      for (int gpKind = 0; gpKind <= 7; ++gpKind) {
        for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
          for (int hasUVCoord = 0; hasUVCoord <= 1; ++hasUVCoord) {
            for (int textureCount = 0; textureCount <= 1; ++textureCount) {
              for (int hasMask = 0; hasMask <= 1; ++hasMask) {
                for (int xpType = XP_UNSUPPORTED; xpType <= XP_FRAMEBUFFER_FETCH; ++xpType) {
                  for (int hasSubset = 0; hasSubset <= 1; ++hasSubset) {
                    Inputs inputs;
                    inputs.gpKind = gpKind;
                    inputs.hasCoverage = hasCoverage != 0;
                    inputs.hasUVCoord = hasUVCoord != 0;
                    inputs.textureCount = textureCount;
                    inputs.hasMask = hasMask != 0;
                    inputs.hasSubset = hasSubset != 0;
                    inputs.resourceProfile = profile;
                    inputs.xpType = xpType;
                    result.push_back(inputs);
                  }
                }
              }
            }
          }
        }
      }
    }
    for (int xpType = XP_UNSUPPORTED; xpType <= XP_FRAMEBUFFER_FETCH; ++xpType) {
      Inputs inputs;
      inputs.gpKind = 1;
      inputs.hasCoverage = true;
      inputs.textureCount = 1;
      inputs.resourceProfile = AOTResourceProfile::NoiseGraph;
      inputs.hasNoise = true;
      inputs.xpType = xpType;
      result.push_back(inputs);
    }
    return result;
  }

  /// Whole-draw guards (alpha-only target, the GP profile classification, the chain leaf and
  /// budget shapes), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);

 private:
  static bool ComposeGP(const Inputs& inputs, ComposedDims<VertDims>* vert,
                        ComposedDims<FragDims>* frag) {
    if (inputs.xpType < XP_NONE || inputs.xpType > XP_FRAMEBUFFER_FETCH ||
        inputs.textureCount < 0 || inputs.textureCount > 1) {
      return false;
    }
    // Each GP profile fixes the attribute slots its vertex buffers carry. The color attribute is
    // unconditional on every layout (a white filler where the buffer is shared across draws), so
    // only the other slots shape dimensions. Quad and Mesh buffers always carry the coverage slot
    // (providers emit 1.0 for non-AA draws); atlas text buffers ride maskCoord on the uvCoord slot
    // with no coverage slot; the ellipse layout evaluates coverage per pixel and carries no
    // coverage or uvCoord attributes; DefaultGP carries none of the quad attributes and takes
    // coverage from its AA type.
    int gpLayout = 0;
    int hasCoverage = 0;
    int hasUVCoord = 0;
    switch (inputs.gpKind) {
      case 0:
        if (inputs.hasUVCoord) {
          return false;
        }
        // The DefaultGP's coverage attribute is unconditional (the non-AA path triangles carry a
        // constant 1.0 slot), so the matcher always reports coverage: reject the vacuous lattice
        // cell so the enumeration equals the producible set.
        if (!inputs.hasCoverage) {
          return false;
        }
        hasCoverage = 1;
        break;
      case 1:
      case 2:
        if (!inputs.hasCoverage) {
          return false;
        }
        hasCoverage = 1;
        hasUVCoord = inputs.hasUVCoord ? 1 : 0;
        break;
      case 3:
        if (inputs.hasCoverage || !inputs.hasUVCoord) {
          return false;
        }
        // The atlas chain route exists to sample the atlas coverage leaf: the decomposer always
        // attaches it, so a leaf-free atlas chain is structurally producible but semantically
        // vacuous. Rejecting it here keeps the reachable set equal to the compiled set (the gap was
        // previously masked by the IsBuildablePermutation post-check falling back to JIT).
        if (inputs.textureCount == 0) {
          return false;
        }
        hasUVCoord = 1;
        break;
      case 4:
        if (inputs.hasCoverage || inputs.hasUVCoord) {
          return false;
        }
        gpLayout = 1;
        break;
      case 5:
        // HairlineLineGP buffers are (position, edgeDistance, color): the color slot is a white
        // filler (the exact color rides the Color uniform) and the edge AA is evaluated per pixel
        // from the edge-distance varying, so no coverage or uvCoord attribute exists.
        if (inputs.hasCoverage || inputs.hasUVCoord) {
          return false;
        }
        gpLayout = 2;
        break;
      case 6:
      case 7:
        // ComplexEllipseGP buffers are (position, color, ellipseOffset, ellipseRadii, edgeDist,
        // [strokeWidth]): device-space like the ellipse, with per-pixel coverage and no coverage or
        // uvCoord attribute. The stroke form (7) adds the strokeWidth attribute, hence its own
        // layout.
        if (inputs.hasCoverage || inputs.hasUVCoord) {
          return false;
        }
        gpLayout = inputs.gpKind == 6 ? 3 : 4;
        break;
      default:
        return false;
    }
    // Coverage subtrees draw their unit input from the GP coverage: the rect and mesh layouts supply
    // it as the vCoverage varying, and the ellipse, hairline and complex rounded-rect layouts
    // evaluate their per-pixel edge coverage ahead of the chain in the kernel (gpPixelCoverage). The
    // atlas route never carries a subtree: its glyph leaf rides the clip-coverage register instead
    // (AOTChainBuilder).
    // Four-leaf variants apply the mask through a runtime uniform, so the compile-time mask
    // dimension exists only for leaf-free chains, and those stay on the rect layout without quad
    // attributes, the hairline layout (a path clip over a solid or gradient hairline) or the
    // complex rounded-rect layouts.
    int hasMaskTexture = inputs.textureCount == 0 && inputs.hasMask ? 1 : 0;
    if (hasMaskTexture != 0 && (gpLayout == 1 || hasUVCoord != 0)) {
      return false;
    }
    // The per-quad subset is a QuadPerEdgeAAGP attribute that clamps a texture leaf, so it exists
    // only on the quad profile with the four-leaf slots.
    if (inputs.hasSubset && (inputs.gpKind != 1 || inputs.textureCount != 1)) {
      return false;
    }
    // HAS_SOURCE_COORDS and RESOURCE_KIND are assigned by Compose: they depend on the resource
    // profile, not the GP mapping.
    vert->set(VD::GP_LAYOUT, gpLayout);
    vert->set(VD::HAS_COVERAGE, hasCoverage);
    vert->set(VD::HAS_UV_COORD, hasUVCoord);
    vert->set(VD::TEXTURE_COUNT, inputs.textureCount);
    vert->set(VD::HAS_SUBSET, inputs.hasSubset ? 1 : 0);
    frag->set(FD::GP_LAYOUT, gpLayout);
    frag->set(FD::HAS_XP, inputs.xpType);
    frag->set(FD::HAS_COVERAGE, hasCoverage);
    frag->set(FD::TEXTURE_COUNT, inputs.textureCount);
    frag->set(FD::HAS_MASK_TEXTURE, hasMaskTexture);
    frag->set(FD::HAS_SUBSET, inputs.hasSubset ? 1 : 0);
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::PointwiseChainShader)
