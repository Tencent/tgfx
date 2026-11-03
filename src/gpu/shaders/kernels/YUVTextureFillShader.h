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
//  either express or implied. see the license for the specific language governing permissions
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/FillKernel.h"

namespace tgfx {

/**
 * YUVTextureFillShader: multi-plane video fill — a QuadPerEdgeAA quad without a subset, whose
 * color is the IR's multi-plane source leaf with the bounded color-grade tail above it (the node
 * IS the contract; no processor-tree walk). A device mask stays a runtime uniform, so the
 * acceptance is defined in the family's .cpp.
 */
class YUVTextureFillShader : public FillKernel<YUVTextureFillShader> {
 public:
  static constexpr const char* Name = "YUVTextureFillShader";
  static constexpr const char* VertFile = "kernels/yuv_texture_fill.vert";
  static constexpr const char* FragFile = "kernels/yuv_texture_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  /// Dimensions, in the bundle's dimension order:
  ///   vert: HAS_UV_COORD (the inverted uv-matrix flag: 0 = a uv matrix, 1 = the uvCoord attribute)
  ///   frag: YUV_FORMAT (0 = I420, 1 = NV12), HAS_XP
  static constexpr Dim vertDims[] = {{"HAS_UV_COORD", 2}};
  static constexpr Dim fragDims[] = {{"YUV_FORMAT", 2}, {XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
