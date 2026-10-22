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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

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
///   fills with the view matrix and QuadPerEdgeAAGP fills with identity (bit-exact).
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
class PointwiseChainShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t {
      GP_LAYOUT,
      HAS_COVERAGE,
      HAS_UV_COORD,
      TEXTURE_COUNT,
      HAS_SUBSET,
      HAS_SOURCE_COORDS,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_LAYOUT", 5),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_UV_COORD"),
          // 0 = no texture leaves, 1 = the four-leaf artifacts (chains with 1-3 sampler
          // children bind phantom padding and run them).
          PermutationInt("TEXTURE_COUNT", 2),
          // Appended so every subset-free index keeps its value.
          PermutationBool("HAS_SUBSET"),
          // The resource kinds (RESOURCE_KIND != 0) carry the SourceCoords varying and reserve
          // CoordTransformMatrix_0 for the source module. Mirrored from the fragment tail; plain
          // chains keep their pre-merge indices.
          PermutationBool("HAS_SOURCE_COORDS"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t {
      GP_LAYOUT,
      HAS_XP,
      HAS_COVERAGE,
      TEXTURE_COUNT,
      HAS_MASK_TEXTURE,
      HAS_SUBSET,
      RESOURCE_KIND,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_LAYOUT", 5),
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
          PermutationInt("TEXTURE_COUNT", 2),
          PermutationBool("HAS_MASK_TEXTURE"),
          PermutationBool("HAS_SUBSET"),
          // 0 = plain chain (16 slots), 1 = gradient source (16 slots), 2 = noise source
          // (32 slots). Appended last so plain chains keep their pre-merge indices; the noise
          // kind holds the largest value so a future removal re-encodes nothing below it.
          PermutationInt("RESOURCE_KIND", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 7, "Update the Compose mapping when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"PointwiseChainShader",
            "level1/pointwise_chain.vert",
            "level1/pointwise_chain.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShaderTextureReadSwizzle};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::PointwiseChainShader, tgfx::EnumeratePointwiseChainReachable)
