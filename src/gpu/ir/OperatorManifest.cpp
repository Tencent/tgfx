/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include "gpu/ir/OperatorManifest.h"

namespace tgfx {
namespace {

const std::vector<OperatorDesc>& Operators() {
  static const std::vector<OperatorDesc> operators = {
      {OperatorID::Luma,
       "Luma",
       "pmApplyLuma",
       SLType::Float4,
       {SLType::Float4, SLType::Float, SLType::Float, SLType::Float},
       nullptr,
       {"ops/Luma.glsl"}},
      {OperatorID::ConstColor,
       "ConstColor",
       "pmApplyConstColor",
       SLType::Float4,
       {SLType::Float4, SLType::Float4, SLType::Int},
       nullptr,
       {"ops/ConstColor.glsl"}},
      {OperatorID::ColorMatrix,
       "ColorMatrix",
       "pmApplyColorMatrix",
       SLType::Float4,
       {SLType::Float4, SLType::Float4x4, SLType::Float4},
       nullptr,
       {"ops/ColorMatrix.glsl"}},
      {OperatorID::AlphaThreshold,
       "AlphaThreshold",
       "pmApplyAlphaThreshold",
       SLType::Float4,
       {SLType::Float4, SLType::Float},
       nullptr,
       {"ops/AlphaThreshold.glsl"}},
      {OperatorID::YUVToRGB,
       "YUVToRGB",
       "tgfxYUVToRGB",
       SLType::Float4,
       {SLType::Float3, SLType::Float3x3, SLType::Int, SLType::Int, SLType::Float},
       nullptr,
       {"fn/effects/yuv_to_rgb.inc"}},
      {OperatorID::Clamp,
       "Clamp",
       "clamp",
       SLType::Float4,
       {SLType::Float4, SLType::Float, SLType::Float}},
      {OperatorID::DeviceCoord,
       "DeviceCoord",
       "tgfxDeviceCoord",
       SLType::Float2,
       {SLType::Float3x3, SLType::Float2},
       nullptr,
       {"fn/source/tgfx_device_coord.inc"}},
      {OperatorID::MulAlpha,
       "MulAlpha",
       "tgfxMulAlpha",
       SLType::Float4,
       {SLType::Float4, SLType::Float4},
       nullptr,
       {"fn/ops/mul_alpha.inc"}},
      {OperatorID::AlphaModulate,
       "AlphaModulate",
       "tgfxAlphaModulate",
       SLType::Float4,
       {SLType::Float4, SLType::Float4},
       nullptr,
       {"fn/source/tgfx_alpha_modulate.inc"}},
      {OperatorID::GaussianBlur1D,
       "GaussianBlur1D",
       "gaussianBlur1D",
       SLType::Float4,
       {SLType::Texture2DSampler, SLType::Float2, SLType::Float2, SLType::Int, SLType::Float4,
        SLType::Int, SLType::Int, SLType::Float4, SLType::Float2, SLType::Int, SLType::Int,
        OperatorParam(SLType::Float4, true, 17)},
       nullptr,
       {"source/texture_sampling.inc", "fn/source/tgfx_texture_sample.inc",
        "fn/source/tiled_map_coord.inc", "fn/source/tiled_seam_blend.inc",
        "fn/source/tiled_apply_border.inc", "fn/source/gaussianBlur1D.inc"}},
      {OperatorID::TiledSample1,
       "TiledSample1",
       "tgfxTiledSample1",
       SLType::Float4,
       {SLType::Texture2DSampler, SLType::Float2},
       nullptr,
       {"fn/source/tgfx_tiled_sample1.inc"}},
      {OperatorID::TiledSample2,
       "TiledSample2",
       "tgfxTiledSample2",
       SLType::Float4,
       {SLType::Texture2DSampler, SLType::Float2, SLType::Float2, SLType::Float},
       nullptr,
       {"fn/source/tgfx_tiled_sample2.inc"}},
      {OperatorID::TiledSample4,
       "TiledSample4",
       "tgfxTiledSample4",
       SLType::Float4,
       {SLType::Texture2DSampler, SLType::Float2, SLType::Float2, SLType::Float2, SLType::Float2,
        SLType::Float, SLType::Float},
       nullptr,
       {"fn/source/tgfx_tiled_sample4.inc"}},
      {OperatorID::TiledSample1Rect,
       "TiledSample1Rect",
       "tgfxTiledSample1Rect",
       SLType::Float4,
       {SLType::Texture2DRectSampler, SLType::Float2},
       nullptr,
       {"fn/source/tgfx_tiled_sample1_rect.inc"}},
      {OperatorID::TiledSample2Rect,
       "TiledSample2Rect",
       "tgfxTiledSample2Rect",
       SLType::Float4,
       {SLType::Texture2DRectSampler, SLType::Float2, SLType::Float2, SLType::Float},
       nullptr,
       {"fn/source/tgfx_tiled_sample2_rect.inc"}},
      {OperatorID::TiledSample4Rect,
       "TiledSample4Rect",
       "tgfxTiledSample4Rect",
       SLType::Float4,
       {SLType::Texture2DRectSampler, SLType::Float2, SLType::Float2, SLType::Float2,
        SLType::Float2, SLType::Float, SLType::Float},
       nullptr,
       {"fn/source/tgfx_tiled_sample4_rect.inc"}},
      {OperatorID::TiledRepeatBlend,
       "TiledRepeatBlend",
       "tgfxTiledRepeatBlend",
       SLType::Float4,
       {SLType::Texture2DSampler, SLType::Float4, SLType::Float2, SLType::Float2, SLType::Float,
        SLType::Float, SLType::Int, SLType::Int},
       nullptr,
       {"fn/source/tgfx_tiled_repeat_blend.inc"}},
      {OperatorID::TiledRepeatBlendRect,
       "TiledRepeatBlendRect",
       "tgfxTiledRepeatBlendRect",
       SLType::Float4,
       {SLType::Texture2DRectSampler, SLType::Float4, SLType::Float2, SLType::Float2, SLType::Float,
        SLType::Float, SLType::Int, SLType::Int},
       nullptr,
       {"fn/source/tgfx_tiled_repeat_blend_rect.inc"}},
      {OperatorID::TiledBorderLinear,
       "TiledBorderLinear",
       "tgfxTiledBorderLinear",
       SLType::Float4,
       {SLType::Float4, SLType::Float, SLType::Float, SLType::Int, SLType::Int},
       nullptr,
       {"fn/source/tgfx_tiled_border_linear.inc"}},
      {OperatorID::TiledBorderNearest,
       "TiledBorderNearest",
       "tgfxTiledBorderNearest",
       SLType::Float4,
       {SLType::Float4, SLType::Float2, SLType::Float4, SLType::Int, SLType::Int},
       nullptr,
       {"fn/source/tgfx_tiled_border_nearest.inc"}},
      {OperatorID::NonAARRectCoverage,
       "NonAARRectCoverage",
       "nonAARRectCoverage",
       SLType::Float,
       {SLType::Float2, SLType::Float4, SLType::Float2, SLType::Float2, SLType::Int},
       nullptr,
       {"geometry/rrect_coverage.inc"}},
      {OperatorID::ComplexRRectCoverage,
       "ComplexRRectCoverage",
       "complexRRectCoverage",
       SLType::Float,
       {SLType::Float2, SLType::Float4, SLType::Float4, SLType::Float4, SLType::Float2,
        SLType::Int},
       nullptr,
       {"geometry/complex_rrect_coverage.inc"}},
      {OperatorID::EllipseCoverage,
       "EllipseCoverage",
       "ellipseEdgeCoverage",
       SLType::Float,
       {SLType::Float2, SLType::Float4, SLType::Int},
       nullptr,
       {"geometry/ellipse_coverage.inc"}},
      {OperatorID::ComplexEllipseCoverage,
       "ComplexEllipseCoverage",
       "complexEllipseEdgeCoverage",
       SLType::Float,
       {SLType::Float2, SLType::Float4, SLType::Float2, SLType::Float2, SLType::Int},
       nullptr,
       {"geometry/complex_ellipse_coverage.inc"}},
      {OperatorID::RRectCoverage,
       "RRectCoverage",
       "pmRRectEffect",
       SLType::Float4,
       {SLType::Float4, SLType::Float2, SLType::Float4, SLType::Float4, SLType::Float4,
        SLType::Float, SLType::Float3x3},
       nullptr,
       {"fn/ops/rrect_effect.inc"}},
      {OperatorID::RectCoverage,
       "RectCoverage",
       "pmRectEffect",
       SLType::Float4,
       {SLType::Float4, SLType::Float2, SLType::Float4, SLType::Float, SLType::Float3x3},
       nullptr,
       {"fn/ops/rect_effect.inc"}},
  };
  return operators;
}

}  // namespace

const OperatorDesc* FindOperator(OperatorID id) {
  for (const auto& op : Operators()) {
    if (op.id == id) {
      return &op;
    }
  }
  return nullptr;
}

size_t OperatorCount() {
  return Operators().size();
}

const OperatorDesc* OperatorAt(size_t index) {
  return index < Operators().size() ? &Operators()[index] : nullptr;
}

}  // namespace tgfx
