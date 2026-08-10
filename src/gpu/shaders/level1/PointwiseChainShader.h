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
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
///   HAS_COVERAGE (bool): per-vertex AA coverage. QuadGP vertex buffers always carry the coverage
///     slot (providers write 1.0 for non-AA draws), so ShouldCompile fixes this to 1 for QuadGP;
///     only DefaultGP toggles it.
///   HAS_UV_COORD (bool): QuadGP only; leaf coords come from the uvCoord attribute instead of
///     aPosition.
///   HAS_COLOR (bool): QuadGP only; the per-vertex color slot exists only when the provider
///     carries per-vertex colors, so it stays a real dimension (mirrored with the fragment stage,
///     which reads vColor instead of the Color uniform when set).
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
    enum : uint32_t { GP_TYPE, HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, TEXTURE_COUNT, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_UV_COORD"),
          PermutationBool("HAS_COLOR"),
          PermutationInt("TEXTURE_COUNT", 4),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_COVERAGE, HAS_COLOR, TEXTURE_COUNT, HAS_MASK_TEXTURE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COLOR"),
          PermutationInt("TEXTURE_COUNT", 4),
          PermutationBool("HAS_MASK_TEXTURE"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 5, "Update ShouldCompile when fragment dimensions change.");

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
    // HAS_COVERAGE / HAS_COLOR / TEXTURE_COUNT vertex-fragment agreement is enforced by the
    // framework (MirroredDimsAgree).
    int gpType = vertValues[VD::GP_TYPE];
    if (gpType == 0) {
      // uvCoord and per-vertex colors are QuadGP-only attributes.
      if (vertValues[VD::HAS_UV_COORD] != 0 || vertValues[VD::HAS_COLOR] != 0) {
        return false;
      }
    } else {
      // The QuadGP coverage slot is always present in the vertex buffer, so the always-on
      // coverage path serves every quad draw and the toggled variants would be dead duplicates.
      if (vertValues[VD::HAS_COVERAGE] == 0) {
        return false;
      }
      // Mask clips go through DefaultGP paths only.
      if (fragValues[FD::HAS_MASK_TEXTURE] != 0) {
        return false;
      }
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::PointwiseChainShader)
