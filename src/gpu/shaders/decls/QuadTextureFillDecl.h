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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <iterator>
#include "gpu/shaders/decls/ModuleDecl.h"

namespace tgfx {

/// QuadTextureFillShader: QuadPerEdgeAAGeometryProcessor + TextureEffect + EmptyXferProcessor/
/// PorterDuffXP. The block mixes family fields with the shared clip and XP blocks in the order the
/// handwritten uniform block used; the device mask and local mask samplers are always declared and
/// selected at runtime, while the per-quad subset varying is a dimension.
struct QuadTextureFillDecl {
  static constexpr const char* Name = "QuadTextureFillShader";
  static constexpr const char* File = "kernels/quad_texture_fill.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Field, Float4, Rect),
      TGFX_DECL_ELEMENT(Field, Int, HasClip),
      TGFX_DECL_ELEMENT(Module, "coverage/rrect_clip_uniforms.inc"),
      TGFX_DECL_ELEMENT(Field, Float4, Subset),
      TGFX_DECL_ELEMENT(Field, Float2, AlphaStart),
      TGFX_DECL_ELEMENT(Field, Float3x3, DeviceCoordMatrix),
      TGFX_DECL_ELEMENT(Field, Float4, DeviceMaskSubset),
      TGFX_DECL_ELEMENT(Field, Int, HasDeviceMask),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
      TGFX_DECL_ELEMENT(Field, Int, AlphaOnly),
      TGFX_DECL_ELEMENT(Field, Int, HasRgbaaa),
      TGFX_DECL_ELEMENT(Field, Int, OutputAlphaSwizzle),
      TGFX_DECL_ELEMENT(Field, Int, SubsetClampOn),
      TGFX_DECL_ELEMENT(Field, Float4, Color),
      TGFX_DECL_ELEMENT(Field, Int, HasCommonColor),
  };

  static constexpr VaryingDecl Varyings[] = {
      {"vec3", "TransformedCoords_0", 0, nullptr, nullptr},
      {"float", "vCoverage", 1, nullptr, nullptr},
      {"vec4", "vColor", 2, nullptr, nullptr},
      {"vec4", "vTexSubset", 3, nullptr, "HAS_SUBSET"},
      {"vec3", "TransformedCoords_1", 4, nullptr, "HAS_LOCAL_MASK"},
  };

  static constexpr SamplerDecl Samplers[] = {
      {"TextureSampler_0", 0, 1, nullptr, nullptr},
      {"MaskTextureSampler", 1, 1, nullptr, nullptr},
      {"LocalMaskSampler", 2, 1, nullptr, "HAS_LOCAL_MASK"},
  };

  static constexpr StepDecl Steps[] = {
      {"FILL_COLOR", "FILL_COLOR_VARYING_OR_COMMON", nullptr},
      {"FILL_SOURCE", "FILL_SOURCE_TEXTURE", nullptr},
      {"FILL_COVERAGE", "FILL_COVERAGE_VERTEX", nullptr},
      {"FILL_DEVICE_MASK", "FILL_DEVICE_MASK_RUNTIME", nullptr},
      {"FILL_LOCAL_MASK", "HAS_LOCAL_MASK", nullptr},
      {"FILL_CLIP", "1", nullptr},
      {"FILL_OUTPUT_SWIZZLE", "1", nullptr},
  };

  static constexpr FragmentFamilyDecl Decl = {Name,
                                              File,
                                              Block,
                                              std::size(Block),
                                              Varyings,
                                              std::size(Varyings),
                                              Samplers,
                                              std::size(Samplers),
                                              Steps,
                                              std::size(Steps),
                                              nullptr,
                                              0,
                                              2,
                                              3,
                                              "HAS_LOCAL_MASK"};
};

}  // namespace tgfx
