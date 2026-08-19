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

#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/level1/ComplexEllipseFillShader.h"
#include "gpu/shaders/level1/ConstColorShader.h"
#include "gpu/shaders/level1/EllipseFillShader.h"
#include "gpu/shaders/level1/HairlineLineShader.h"
#include "gpu/shaders/level1/HairlineQuadShader.h"
#include "gpu/shaders/level1/MaskFillShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadConstColorShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/SolidColorFillShader.h"

namespace tgfx {

std::optional<RuleComposedValues> ComposeRoundStrokeRect(const RoundStrokeRectInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = RoundStrokeRectFillShader::Dims;
  using FD = RoundStrokeRectFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_AA] = inputs.isCoverageAA ? 1 : 0;
  values.vertValues[D::HAS_COMMON_COLOR] = inputs.hasCommonColor ? 1 : 0;
  values.vertValues[D::HAS_UV_MATRIX] = inputs.hasUVMatrix ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_AA] = values.vertValues[D::HAS_AA];
  values.fragValues[FD::HAS_COMMON_COLOR] = values.vertValues[D::HAS_COMMON_COLOR];
  values.fragValues[FD::HAS_UV_MATRIX] = values.vertValues[D::HAS_UV_MATRIX];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateRoundStrokeRectReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int isCoverageAA = 0; isCoverageAA <= 1; ++isCoverageAA) {
    for (int hasCommonColor = 0; hasCommonColor <= 1; ++hasCommonColor) {
      for (int hasUVMatrix = 0; hasUVMatrix <= 1; ++hasUVMatrix) {
        for (int xpType = -1; xpType <= 2; ++xpType) {
          RoundStrokeRectInputs inputs;
          inputs.isCoverageAA = isCoverageAA != 0;
          inputs.hasCommonColor = hasCommonColor != 0;
          inputs.hasUVMatrix = hasUVMatrix != 0;
          inputs.xpType = xpType;
          auto composed = ComposeRoundStrokeRect(inputs);
          if (!composed) {
            continue;
          }
          auto vertIndex = RoundStrokeRectFillShader::Dims::domain().encode(composed->vertValues);
          auto fragIndex = RoundStrokeRectFillShader::FD::domain().encode(composed->fragValues);
          result.insert({vertIndex, fragIndex});
        }
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeMaskFill(const MaskFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = MaskFillShader::D;
  RuleComposedValues values;
  values.fragValues.resize(D::COUNT);
  values.fragValues[D::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateMaskFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int xpType = -1; xpType <= 2; ++xpType) {
    MaskFillInputs inputs;
    inputs.xpType = xpType;
    auto composed = ComposeMaskFill(inputs);
    if (!composed) {
      continue;
    }
    auto fragIndex = MaskFillShader::D::domain().encode(composed->fragValues);
    result.insert({0, fragIndex});
  }
  return result;
}

std::optional<RuleComposedValues> ComposeConstColor(const ConstColorInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = ConstColorShader::FragDims;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  // inputMode is a runtime uniform (InputMode), not a permutation dimension.
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateConstColorReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int xpType = -1; xpType <= 2; ++xpType) {
    ConstColorInputs inputs;
    inputs.xpType = xpType;
    auto composed = ComposeConstColor(inputs);
    if (!composed) {
      continue;
    }
    auto fragIndex = ConstColorShader::FragDims::domain().encode(composed->fragValues);
    result.insert({0, fragIndex});
  }
  return result;
}

std::optional<RuleComposedValues> ComposeHairlineLine(const HairlineLineInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = HairlineLineShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateHairlineLineReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int xpType = -1; xpType <= 2; ++xpType) {
    HairlineLineInputs inputs;
    inputs.xpType = xpType;
    auto composed = ComposeHairlineLine(inputs);
    if (!composed) {
      continue;
    }
    auto fragIndex = HairlineLineShader::FD::domain().encode(composed->fragValues);
    result.insert({0, fragIndex});
  }
  return result;
}

std::optional<RuleComposedValues> ComposeHairlineQuad(const HairlineQuadInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = HairlineQuadShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateHairlineQuadReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int xpType = -1; xpType <= 2; ++xpType) {
    HairlineQuadInputs inputs;
    inputs.xpType = xpType;
    auto composed = ComposeHairlineQuad(inputs);
    if (!composed) {
      continue;
    }
    auto fragIndex = HairlineQuadShader::FD::domain().encode(composed->fragValues);
    result.insert({0, fragIndex});
  }
  return result;
}

std::optional<RuleComposedValues> ComposeQuadConstColor(const QuadConstColorInputs& inputs) {
  using VD = QuadConstColorShader::VD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_UV_COORD] = inputs.hasUVMatrix ? 0 : 1;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadConstColorReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasUVMatrix = 0; hasUVMatrix <= 1; ++hasUVMatrix) {
    QuadConstColorInputs inputs;
    inputs.hasUVMatrix = hasUVMatrix != 0;
    auto composed = ComposeQuadConstColor(inputs);
    if (!composed) {
      continue;
    }
    auto vertIndex = QuadConstColorShader::VD::domain().encode(composed->vertValues);
    result.insert({vertIndex, 0});
  }
  return result;
}

std::optional<RuleComposedValues> ComposeQuadColorFill(const QuadColorFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = QuadColorFillShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_MASK_TEXTURE] = inputs.hasMaskTexture ? 1 : 0;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadColorFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int xpType = -1; xpType <= 2; ++xpType) {
    for (int hasMaskTexture = 0; hasMaskTexture <= 1; ++hasMaskTexture) {
      QuadColorFillInputs inputs;
      inputs.xpType = xpType;
      inputs.hasMaskTexture = hasMaskTexture != 0;
      auto composed = ComposeQuadColorFill(inputs);
      if (!composed) {
        continue;
      }
      auto fragIndex = QuadColorFillShader::FD::domain().encode(composed->fragValues);
      result.insert({0, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeSolidColorFill(const SolidColorFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = SolidColorFillShader::VD;
  using FD = SolidColorFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_COVERAGE] = inputs.isCoverageAA ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[VD::HAS_COVERAGE];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateSolidColorFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int isCoverageAA = 0; isCoverageAA <= 1; ++isCoverageAA) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      SolidColorFillInputs inputs;
      inputs.isCoverageAA = isCoverageAA != 0;
      inputs.xpType = xpType;
      auto composed = ComposeSolidColorFill(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex = SolidColorFillShader::VD::domain().encode(composed->vertValues);
      auto fragIndex = SolidColorFillShader::FD::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeComplexEllipseFill(
    const ComplexEllipseFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = ComplexEllipseFillShader::Dims;
  using FD = ComplexEllipseFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::STROKE] = inputs.isStroke ? 1 : 0;
  values.vertValues[D::HAS_COMMON_COLOR] = inputs.hasCommonColor ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::STROKE] = values.vertValues[D::STROKE];
  values.fragValues[FD::HAS_COMMON_COLOR] = values.vertValues[D::HAS_COMMON_COLOR];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexEllipseFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int isStroke = 0; isStroke <= 1; ++isStroke) {
    for (int hasCommonColor = 0; hasCommonColor <= 1; ++hasCommonColor) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        ComplexEllipseFillInputs inputs;
        inputs.isStroke = isStroke != 0;
        inputs.hasCommonColor = hasCommonColor != 0;
        inputs.xpType = xpType;
        auto composed = ComposeComplexEllipseFill(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = ComplexEllipseFillShader::Dims::domain().encode(composed->vertValues);
        auto fragIndex = ComplexEllipseFillShader::FD::domain().encode(composed->fragValues);
        result.insert({vertIndex, fragIndex});
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeEllipseFill(const EllipseFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = EllipseFillShader::Dims;
  using FD = EllipseFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_COMMON_COLOR] = inputs.hasCommonColor ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_COMMON_COLOR] = values.vertValues[D::HAS_COMMON_COLOR];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_DEVICE_MASK] = inputs.deviceMask;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateEllipseFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCommonColor = 0; hasCommonColor <= 1; ++hasCommonColor) {
    for (int deviceMask = 0; deviceMask <= 1; ++deviceMask) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        EllipseFillInputs inputs;
        inputs.hasCommonColor = hasCommonColor != 0;
        inputs.deviceMask = deviceMask;
        inputs.xpType = xpType;
        auto composed = ComposeEllipseFill(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = EllipseFillShader::Dims::domain().encode(composed->vertValues);
        auto fragIndex = EllipseFillShader::FD::domain().encode(composed->fragValues);
        result.insert({vertIndex, fragIndex});
      }
    }
  }
  return result;
}

std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName) {
  if (shaderName == "RoundStrokeRectFillShader") {
    return EnumerateRoundStrokeRectReachable();
  }
  if (shaderName == "MaskFillShader") {
    return EnumerateMaskFillReachable();
  }
  if (shaderName == "ConstColorShader") {
    return EnumerateConstColorReachable();
  }
  if (shaderName == "HairlineLineShader") {
    return EnumerateHairlineLineReachable();
  }
  if (shaderName == "HairlineQuadShader") {
    return EnumerateHairlineQuadReachable();
  }
  if (shaderName == "QuadConstColorShader") {
    return EnumerateQuadConstColorReachable();
  }
  if (shaderName == "QuadColorFillShader") {
    return EnumerateQuadColorFillReachable();
  }
  if (shaderName == "SolidColorFillShader") {
    return EnumerateSolidColorFillReachable();
  }
  if (shaderName == "ComplexEllipseFillShader") {
    return EnumerateComplexEllipseFillReachable();
  }
  if (shaderName == "EllipseFillShader") {
    return EnumerateEllipseFillReachable();
  }
  return std::nullopt;
}

}  // namespace tgfx
