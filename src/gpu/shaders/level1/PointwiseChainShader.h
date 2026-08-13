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
/// fused pass. The DAG shape is runtime data: every node occupies one of 16 statically expanded
/// slots whose OpType and two input-slot indices are uniforms, so any topology hits the same
/// variant. Only the texture-leaf count is a compile-time dimension, because each leaf adds a
/// sampler binding and a TransformedCoords varying.
///
/// Vertex dimensions:
///   HAS_COVERAGE (bool): per-vertex AA coverage varying present.
///   HAS_UV_COORD (bool): quad only; leaf coords come from the uvCoord attribute instead of
///     aPosition.
///   HAS_COLOR (bool): quad only; the per-vertex color slot exists only when the provider
///     carries per-vertex colors (mirrored with the fragment stage, which reads vColor instead
///     of the Color uniform when set).
///   There is no GP_TYPE: the position always goes through the Matrix uniform, which DefaultGP
///   fills with the view matrix and QuadPerEdgeAAGP fills with identity (bit-exact).
///   TEXTURE_COUNT (int, 4 values): 0 -> 0 leaves, 1 -> 1, 2 -> 2, 3 -> 4. A zero-leaf chain
///     evaluates const-color/blend ops against the geometry color alone.
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
///   HAS_COVERAGE / HAS_COLOR / TEXTURE_COUNT: mirrored with the vertex stage.
///   HAS_MASK_TEXTURE (bool): a device-space alpha mask child sampled after the DAG and before the
///     XP stage, matching the legacy blend kernel's mask application point. DefaultGP only.
class PointwiseChainShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { GP_LAYOUT, HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, TEXTURE_COUNT, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_LAYOUT", 2),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_UV_COORD"),
          PermutationBool("HAS_COLOR"),
          // 0 = no texture leaves, 1 = the four-leaf artifacts (chains with 1-3 sampler
          // children bind phantom padding and run them).
          PermutationInt("TEXTURE_COUNT", 2),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t {
      GP_LAYOUT,
      HAS_XP,
      HAS_COVERAGE,
      HAS_COLOR,
      TEXTURE_COUNT,
      HAS_MASK_TEXTURE,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_LAYOUT", 2),
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COLOR"),
          PermutationInt("TEXTURE_COUNT", 2),
          PermutationBool("HAS_MASK_TEXTURE"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 6, "Update ShouldCompile when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"PointwiseChainShader",
            "level1/pointwise_chain.vert",
            "level1/pointwise_chain.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>& vertValues,
                            const std::vector<int>& fragValues) {
    // GP_LAYOUT / HAS_COVERAGE / HAS_COLOR / TEXTURE_COUNT vertex-fragment agreement is enforced
    // by the framework (MirroredDimsAgree).
    if (vertValues[VD::GP_LAYOUT] == 1) {
      // The ellipse layout carries no coverage/uvCoord attributes (its coverage is the per-pixel
      // edge equation) and never takes a mask clip. Leaf-free chains (const/blend/gradient) are
      // served as well: only solid fills stay on EllipseFillShader via the plain route.
      if (vertValues[VD::HAS_COVERAGE] != 0 || vertValues[VD::HAS_UV_COORD] != 0 ||
          fragValues[FD::HAS_MASK_TEXTURE] != 0) {
        return false;
      }
      return true;
    }
    // uvCoord and per-vertex colors are quad-only attributes, and quad vertex buffers always
    // carry the coverage slot, so either of them implies coverage. Exception: atlas text buffers
    // (position, maskCoord, color?) carry no coverage slot, so the atlas route requests
    // coverage-free uvCoord combos; those are only useful with the atlas leaf present (1-2
    // texture leaves: the atlas alone for gradient text, image+atlas for image text).
    if ((vertValues[VD::HAS_UV_COORD] != 0 || vertValues[VD::HAS_COLOR] != 0) &&
        vertValues[VD::HAS_COVERAGE] == 0) {
      bool atlasTextCombo = vertValues[VD::HAS_UV_COORD] != 0 && vertValues[VD::TEXTURE_COUNT] == 1;
      if (!atlasTextCombo) {
        return false;
      }
    }
    // Mask clips go through DefaultGP paths only, which never carry quad attributes.
    if (fragValues[FD::HAS_MASK_TEXTURE] != 0 &&
        (vertValues[VD::HAS_UV_COORD] != 0 || vertValues[VD::HAS_COLOR] != 0)) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::PointwiseChainShader)
