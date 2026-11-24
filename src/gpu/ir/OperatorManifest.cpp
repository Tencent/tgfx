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
       {SLType::Float4, SLType::Float, SLType::Float, SLType::Float}},
      {OperatorID::ConstColor,
       "ConstColor",
       "pmApplyConstColor",
       SLType::Float4,
       {SLType::Float4, SLType::Float4, SLType::Int}},
      {OperatorID::ColorMatrix,
       "ColorMatrix",
       "pmApplyColorMatrix",
       SLType::Float4,
       {SLType::Float4, SLType::Float4x4, SLType::Float4}},
      {OperatorID::AlphaThreshold,
       "AlphaThreshold",
       "pmApplyAlphaThreshold",
       SLType::Float4,
       {SLType::Float4, SLType::Float}},
      {OperatorID::YUVToRGB,
       "YUVToRGB",
       "tgfxYUVToRGB",
       SLType::Float4,
       {SLType::Float3, SLType::Float3x3, SLType::Int, SLType::Int, SLType::Float}},
      {OperatorID::Clamp,
       "Clamp",
       "clamp",
       SLType::Float4,
       {SLType::Float4, SLType::Float, SLType::Float}},
      {OperatorID::DeviceCoord,
       "DeviceCoord",
       "tgfxDeviceCoord",
       SLType::Float2,
       {SLType::Float3x3, SLType::Float2}},
      {OperatorID::MulAlpha,
       "MulAlpha",
       "tgfxMulAlpha",
       SLType::Float4,
       {SLType::Float4, SLType::Float4}},
      {OperatorID::AlphaModulate,
       "AlphaModulate",
       "tgfxAlphaModulate",
       SLType::Float4,
       {SLType::Float4, SLType::Float4}},
      {OperatorID::GaussianBlur1D,
       "GaussianBlur1D",
       "gaussianBlur1D",
       SLType::Float4,
       {SLType::Texture2DSampler, SLType::Float2, SLType::Float2, SLType::Int, SLType::Float4,
        SLType::Int, SLType::Int, SLType::Float4, SLType::Float2, SLType::Int, SLType::Int,
        SLType::Float4}},
      {OperatorID::TiledSample,
       "TiledSample",
       "tgfxTiledSample",
       SLType::Float4,
       {SLType::Texture2DSampler, SLType::Float2, SLType::Float4, SLType::Float4, SLType::Float2,
        SLType::Int, SLType::Int, SLType::Int, SLType::Float4}},
      {OperatorID::TiledSampleRect,
       "TiledSampleRect",
       "tgfxTiledSampleRect",
       SLType::Float4,
       {SLType::Texture2DRectSampler, SLType::Float2, SLType::Float4, SLType::Float4,
        SLType::Float2, SLType::Int, SLType::Int, SLType::Int, SLType::Float4}},
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

}  // namespace tgfx
