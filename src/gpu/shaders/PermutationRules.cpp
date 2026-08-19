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
#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include "gpu/shaders/level1/ComplexEllipseFillShader.h"
#include "gpu/shaders/level1/ComplexNonAARRectFillShader.h"
#include "gpu/shaders/level1/ConstColorShader.h"
#include "gpu/shaders/level1/GaussianBlur1DShader.h"
#include "gpu/shaders/level1/DeviceSpaceTextureShader.h"
#include "gpu/shaders/level1/DeviceSpaceTexturedEffectShader.h"
#include "gpu/shaders/level1/EllipseFillShader.h"
#include "gpu/shaders/level1/HairlineLineShader.h"
#include "gpu/shaders/level1/HairlineQuadShader.h"
#include "gpu/shaders/level1/MaskFillShader.h"
#include "gpu/shaders/level1/MeshFillShader.h"
#include "gpu/shaders/level1/NonAARRectFillShader.h"
#include "gpu/shaders/level1/PerlinNoiseFillShader.h"
#include "gpu/shaders/level1/PointwiseDirectShader.h"
#include "gpu/shaders/level1/PointwiseTailShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadConstColorShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedTextureCoverageShader.h"
#include "gpu/shaders/level1/SolidColorFillShader.h"
#include "gpu/shaders/level1/TexturedEffectShader.h"
#include "gpu/shaders/level1/TextureColorMatrixShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gpu/shaders/level1/TiledTextureFillShader.h"
#include "gpu/shaders/level1/UnifiedGradientShader.h"
#include "gpu/shaders/level1/YUVTextureFillShader.h"

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

std::optional<RuleComposedValues> ComposeNonAARRectFill(const NonAARRectFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  // The textured variant (a TiledTextureEffect fill) is fill-only: a stroked rect never carries
  // the tiled texture child.
  if (inputs.textured && inputs.isStroke) {
    return std::nullopt;
  }
  using D = NonAARRectFillShader::Dims;
  using FD = NonAARRectFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_COMMON_COLOR] = inputs.hasCommonColor ? 1 : 0;
  values.vertValues[D::STROKE] = inputs.isStroke ? 1 : 0;
  values.vertValues[D::TEXTURED] = inputs.textured ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_COMMON_COLOR] = values.vertValues[D::HAS_COMMON_COLOR];
  values.fragValues[FD::STROKE] = values.vertValues[D::STROKE];
  values.fragValues[FD::TEXTURED] = values.vertValues[D::TEXTURED];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateNonAARRectFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCommonColor = 0; hasCommonColor <= 1; ++hasCommonColor) {
    for (int isStroke = 0; isStroke <= 1; ++isStroke) {
      for (int textured = 0; textured <= 1; ++textured) {
        for (int xpType = -1; xpType <= 2; ++xpType) {
          NonAARRectFillInputs inputs;
          inputs.hasCommonColor = hasCommonColor != 0;
          inputs.isStroke = isStroke != 0;
          inputs.textured = textured != 0;
          inputs.xpType = xpType;
          auto composed = ComposeNonAARRectFill(inputs);
          if (!composed) {
            continue;
          }
          auto vertIndex = NonAARRectFillShader::Dims::domain().encode(composed->vertValues);
          auto fragIndex = NonAARRectFillShader::FD::domain().encode(composed->fragValues);
          result.insert({vertIndex, fragIndex});
        }
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeComplexNonAARRectFill(
    const ComplexNonAARRectFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = ComplexNonAARRectFillShader::Dims;
  using FD = ComplexNonAARRectFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_COMMON_COLOR] = inputs.hasCommonColor ? 1 : 0;
  values.vertValues[D::STROKE] = inputs.isStroke ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_COMMON_COLOR] = values.vertValues[D::HAS_COMMON_COLOR];
  values.fragValues[FD::STROKE] = values.vertValues[D::STROKE];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexNonAARRectFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCommonColor = 0; hasCommonColor <= 1; ++hasCommonColor) {
    for (int isStroke = 0; isStroke <= 1; ++isStroke) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        ComplexNonAARRectFillInputs inputs;
        inputs.hasCommonColor = hasCommonColor != 0;
        inputs.isStroke = isStroke != 0;
        inputs.xpType = xpType;
        auto composed = ComposeComplexNonAARRectFill(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = ComplexNonAARRectFillShader::Dims::domain().encode(composed->vertValues);
        auto fragIndex = ComplexNonAARRectFillShader::FD::domain().encode(composed->fragValues);
        result.insert({vertIndex, fragIndex});
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeMeshFill(const MeshFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = MeshFillShader::Dims;
  using FD = MeshFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_TEX_COORDS] = inputs.hasTexCoords ? 1 : 0;
  values.vertValues[D::HAS_COLOR] = inputs.hasColors ? 1 : 0;
  values.vertValues[D::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_TEX_COORDS] = values.vertValues[D::HAS_TEX_COORDS];
  values.fragValues[FD::HAS_COLOR] = values.vertValues[D::HAS_COLOR];
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[D::HAS_COVERAGE];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateMeshFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasTexCoords = 0; hasTexCoords <= 1; ++hasTexCoords) {
    for (int hasColors = 0; hasColors <= 1; ++hasColors) {
      for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
        for (int xpType = -1; xpType <= 2; ++xpType) {
          MeshFillInputs inputs;
          inputs.hasTexCoords = hasTexCoords != 0;
          inputs.hasColors = hasColors != 0;
          inputs.hasCoverage = hasCoverage != 0;
          inputs.xpType = xpType;
          auto composed = ComposeMeshFill(inputs);
          if (!composed) {
            continue;
          }
          auto vertIndex = MeshFillShader::Dims::domain().encode(composed->vertValues);
          auto fragIndex = MeshFillShader::FD::domain().encode(composed->fragValues);
          result.insert({vertIndex, fragIndex});
        }
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposePerlinNoiseFill(const PerlinNoiseFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = PerlinNoiseFillShader::VD;
  using D = PerlinNoiseFillShader::D;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(D::COUNT);
  values.fragValues[D::HAS_XP] = inputs.xpType;
  values.fragValues[D::HAS_COVERAGE] = values.vertValues[VD::HAS_COVERAGE];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumeratePerlinNoiseFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      PerlinNoiseFillInputs inputs;
      inputs.hasCoverage = hasCoverage != 0;
      inputs.xpType = xpType;
      auto composed = ComposePerlinNoiseFill(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex = PerlinNoiseFillShader::VD::domain().encode(composed->vertValues);
      auto fragIndex = PerlinNoiseFillShader::D::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeTextureColorMatrix(
    const TextureColorMatrixInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = TextureColorMatrixShader::D;
  RuleComposedValues values;
  values.fragValues.resize(D::COUNT);
  // alphaOnly / hasRGBAAA / subset are runtime uniforms, not permutation dimensions.
  values.fragValues[D::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateTextureColorMatrixReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int xpType = -1; xpType <= 2; ++xpType) {
    TextureColorMatrixInputs inputs;
    inputs.xpType = xpType;
    auto composed = ComposeTextureColorMatrix(inputs);
    if (!composed) {
      continue;
    }
    auto fragIndex = TextureColorMatrixShader::D::domain().encode(composed->fragValues);
    result.insert({0, fragIndex});
  }
  return result;
}

std::optional<RuleComposedValues> ComposeYUVTextureFill(const YUVTextureFillInputs& inputs) {
  if (inputs.xpType < 0 || inputs.yuvFormat < 0) {
    return std::nullopt;
  }
  using VD = YUVTextureFillShader::VD;
  using FD = YUVTextureFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_UV_COORD] = inputs.hasUVMatrix ? 0 : 1;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::YUV_FORMAT] = inputs.yuvFormat;
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateYUVTextureFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasUVMatrix = 0; hasUVMatrix <= 1; ++hasUVMatrix) {
    for (int yuvFormat = -1; yuvFormat <= 1; ++yuvFormat) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        YUVTextureFillInputs inputs;
        inputs.hasUVMatrix = hasUVMatrix != 0;
        inputs.yuvFormat = yuvFormat;
        inputs.xpType = xpType;
        auto composed = ComposeYUVTextureFill(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = YUVTextureFillShader::VD::domain().encode(composed->vertValues);
        auto fragIndex = YUVTextureFillShader::FD::domain().encode(composed->fragValues);
        result.insert({vertIndex, fragIndex});
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeDeviceSpaceTexture(
    const DeviceSpaceTextureInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = DeviceSpaceTextureShader::Dims;
  RuleComposedValues values;
  values.vertValues.resize(FD::COUNT);
  values.vertValues[FD::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  // ALPHA_ONLY is a runtime uniform (AlphaOnly), not a permutation dimension.
  values.vertValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues = values.vertValues;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateDeviceSpaceTextureReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      DeviceSpaceTextureInputs inputs;
      inputs.hasCoverage = hasCoverage != 0;
      inputs.xpType = xpType;
      auto composed = ComposeDeviceSpaceTexture(inputs);
      if (!composed) {
        continue;
      }
      auto index = DeviceSpaceTextureShader::Dims::domain().encode(composed->fragValues);
      result.insert({index, index});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeDeviceSpaceTexturedEffect(
    const DeviceSpaceTexturedEffectInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = DeviceSpaceTexturedEffectShader::VD;
  using FD = DeviceSpaceTexturedEffectShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[VD::HAS_COVERAGE];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateDeviceSpaceTexturedEffectReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      DeviceSpaceTexturedEffectInputs inputs;
      inputs.hasCoverage = hasCoverage != 0;
      inputs.xpType = xpType;
      auto composed = ComposeDeviceSpaceTexturedEffect(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex =
          DeviceSpaceTexturedEffectShader::VD::domain().encode(composed->vertValues);
      auto fragIndex =
          DeviceSpaceTexturedEffectShader::FD::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeShapeInstancedTextureCoverage(
    const ShapeInstancedTextureCoverageInputs& inputs) {
  // The bare coverage form reads the per-instance color; only the gradient form serves colorless
  // batches with an opaque-white input.
  if (!inputs.gradient && !inputs.hasColors) {
    return std::nullopt;
  }
  using D = ShapeInstancedTextureCoverageShader::D;
  using FD = ShapeInstancedTextureCoverageShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::GRADIENT] = inputs.gradient ? 1 : 0;
  values.vertValues[D::HAS_COLORS] = inputs.hasColors ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::GRADIENT] = values.vertValues[D::GRADIENT];
  values.fragValues[FD::HAS_COLORS] = values.vertValues[D::HAS_COLORS];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateShapeInstancedTextureCoverageReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int gradient = 0; gradient <= 1; ++gradient) {
    for (int hasColors = 0; hasColors <= 1; ++hasColors) {
      ShapeInstancedTextureCoverageInputs inputs;
      inputs.gradient = gradient != 0;
      inputs.hasColors = hasColors != 0;
      auto composed = ComposeShapeInstancedTextureCoverage(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex =
          ShapeInstancedTextureCoverageShader::D::domain().encode(composed->vertValues);
      auto fragIndex =
          ShapeInstancedTextureCoverageShader::FD::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeShapeInstancedFill(
    const ShapeInstancedFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = ShapeInstancedFillShader::Dims;
  using FD = ShapeInstancedFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_COLOR] = inputs.hasColors ? 1 : 0;
  values.vertValues[D::HAS_AA] = inputs.isCoverageAA ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_COLOR] = values.vertValues[D::HAS_COLOR];
  values.fragValues[FD::HAS_AA] = values.vertValues[D::HAS_AA];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateShapeInstancedFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasColors = 0; hasColors <= 1; ++hasColors) {
    for (int isCoverageAA = 0; isCoverageAA <= 1; ++isCoverageAA) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        ShapeInstancedFillInputs inputs;
        inputs.hasColors = hasColors != 0;
        inputs.isCoverageAA = isCoverageAA != 0;
        inputs.xpType = xpType;
        auto composed = ComposeShapeInstancedFill(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = ShapeInstancedFillShader::Dims::domain().encode(composed->vertValues);
        auto fragIndex = ShapeInstancedFillShader::FD::domain().encode(composed->fragValues);
        result.insert({vertIndex, fragIndex});
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeTextureFill(const TextureFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = TextureFillShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_DEVICE_MASK] = inputs.deviceMask;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateTextureFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int deviceMask = 0; deviceMask <= 1; ++deviceMask) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      TextureFillInputs inputs;
      inputs.deviceMask = deviceMask;
      inputs.xpType = xpType;
      auto composed = ComposeTextureFill(inputs);
      if (!composed) {
        continue;
      }
      auto fragIndex = TextureFillShader::FD::domain().encode(composed->fragValues);
      result.insert({0, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeTiledTextureFill(
    const TiledTextureFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = TiledTextureFillShader::VD;
  using FD = TiledTextureFillShader::FragDims;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[VD::HAS_COVERAGE];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateTiledTextureFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      TiledTextureFillInputs inputs;
      inputs.hasCoverage = hasCoverage != 0;
      inputs.xpType = xpType;
      auto composed = ComposeTiledTextureFill(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex = TiledTextureFillShader::VD::domain().encode(composed->vertValues);
      auto fragIndex = TiledTextureFillShader::FragDims::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeQuadTextureFill(const QuadTextureFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = QuadTextureFillShader::VD;
  using FD = QuadTextureFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_UV_COORD] = inputs.hasUVCoord ? 1 : 0;
  values.vertValues[VD::HAS_SUBSET] = inputs.hasSubset ? 1 : 0;
  values.vertValues[VD::HAS_LOCAL_MASK] = inputs.hasLocalMask ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  // ALPHA_ONLY and HAS_RGBAAA are runtime uniforms (set by GLSLTextureEffect::onSetData), not
  // variants.
  values.fragValues[FD::HAS_SUBSET] = values.vertValues[VD::HAS_SUBSET];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_LOCAL_MASK] = values.vertValues[VD::HAS_LOCAL_MASK];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadTextureFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasUVCoord = 0; hasUVCoord <= 1; ++hasUVCoord) {
    for (int hasSubset = 0; hasSubset <= 1; ++hasSubset) {
      for (int hasLocalMask = 0; hasLocalMask <= 1; ++hasLocalMask) {
        for (int xpType = -1; xpType <= 2; ++xpType) {
          QuadTextureFillInputs inputs;
          inputs.hasUVCoord = hasUVCoord != 0;
          inputs.hasSubset = hasSubset != 0;
          inputs.hasLocalMask = hasLocalMask != 0;
          inputs.xpType = xpType;
          auto composed = ComposeQuadTextureFill(inputs);
          if (!composed) {
            continue;
          }
          auto vertIndex = QuadTextureFillShader::VD::domain().encode(composed->vertValues);
          auto fragIndex = QuadTextureFillShader::FD::domain().encode(composed->fragValues);
          result.insert({vertIndex, fragIndex});
        }
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeGaussianBlur1D(const GaussianBlur1DInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = GaussianBlur1DShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateGaussianBlur1DReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int xpType = -1; xpType <= 2; ++xpType) {
    GaussianBlur1DInputs inputs;
    inputs.xpType = xpType;
    auto composed = ComposeGaussianBlur1D(inputs);
    if (!composed) {
      continue;
    }
    auto fragIndex = GaussianBlur1DShader::FD::domain().encode(composed->fragValues);
    result.insert({0, fragIndex});
  }
  return result;
}

std::optional<RuleComposedValues> ComposeTexturedEffect(const TexturedEffectInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = TexturedEffectShader::VD;
  using D = TexturedEffectShader::D;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(D::COUNT);
  values.fragValues[D::HAS_XP] = inputs.xpType;
  values.fragValues[D::HAS_COVERAGE] = values.vertValues[VD::HAS_COVERAGE];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateTexturedEffectReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      TexturedEffectInputs inputs;
      inputs.hasCoverage = hasCoverage != 0;
      inputs.xpType = xpType;
      auto composed = ComposeTexturedEffect(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex = TexturedEffectShader::VD::domain().encode(composed->vertValues);
      auto fragIndex = TexturedEffectShader::D::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposePointwiseTail(const PointwiseTailInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = PointwiseTailShader::VD;
  using FD = PointwiseTailShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[VD::HAS_COVERAGE];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumeratePointwiseTailReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      PointwiseTailInputs inputs;
      inputs.hasCoverage = hasCoverage != 0;
      inputs.xpType = xpType;
      auto composed = ComposePointwiseTail(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex = PointwiseTailShader::VD::domain().encode(composed->vertValues);
      auto fragIndex = PointwiseTailShader::FD::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposePointwiseDirect(const PointwiseDirectInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = PointwiseDirectShader::VD;
  using FD = PointwiseDirectShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[VD::HAS_COVERAGE];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumeratePointwiseDirectReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int xpType = -1; xpType <= 2; ++xpType) {
      PointwiseDirectInputs inputs;
      inputs.hasCoverage = hasCoverage != 0;
      inputs.xpType = xpType;
      auto composed = ComposePointwiseDirect(inputs);
      if (!composed) {
        continue;
      }
      auto vertIndex = PointwiseDirectShader::VD::domain().encode(composed->vertValues);
      auto fragIndex = PointwiseDirectShader::FD::domain().encode(composed->fragValues);
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeUnifiedGradient(const UnifiedGradientInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  // LUT gradients keep the legacy TextureGradientShader behavior: coverage-carrying draws stay on
  // the runtime path.
  if (inputs.hasLUT && inputs.hasVCoverage) {
    return std::nullopt;
  }
  using VD = UnifiedGradientShader::VD;
  using FD = UnifiedGradientShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_VCOVERAGE] = inputs.hasVCoverage ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_VCOVERAGE] = values.vertValues[VD::HAS_VCOVERAGE];
  values.fragValues[FD::HAS_LUT] = inputs.hasLUT ? 1 : 0;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateUnifiedGradientReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasVCoverage = 0; hasVCoverage <= 1; ++hasVCoverage) {
    for (int hasLUT = 0; hasLUT <= 1; ++hasLUT) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        UnifiedGradientInputs inputs;
        inputs.hasVCoverage = hasVCoverage != 0;
        inputs.hasLUT = hasLUT != 0;
        inputs.xpType = xpType;
        auto composed = ComposeUnifiedGradient(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = UnifiedGradientShader::VD::domain().encode(composed->vertValues);
        auto fragIndex = UnifiedGradientShader::FD::domain().encode(composed->fragValues);
        result.insert({vertIndex, fragIndex});
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeAtlasTextFill(const AtlasTextFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = AtlasTextFillShader::D;
  using FD = AtlasTextFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.vertValues[D::HAS_COMMON_COLOR] = inputs.hasCommonColor ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[D::HAS_COVERAGE];
  values.fragValues[FD::HAS_COMMON_COLOR] = values.vertValues[D::HAS_COMMON_COLOR];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateAtlasTextFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
    for (int hasCommonColor = 0; hasCommonColor <= 1; ++hasCommonColor) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        AtlasTextFillInputs inputs;
        inputs.hasCoverage = hasCoverage != 0;
        inputs.hasCommonColor = hasCommonColor != 0;
        inputs.xpType = xpType;
        auto composed = ComposeAtlasTextFill(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = AtlasTextFillShader::D::domain().encode(composed->vertValues);
        auto fragIndex = AtlasTextFillShader::FD::domain().encode(composed->fragValues);
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
  if (shaderName == "NonAARRectFillShader") {
    return EnumerateNonAARRectFillReachable();
  }
  if (shaderName == "ComplexNonAARRectFillShader") {
    return EnumerateComplexNonAARRectFillReachable();
  }
  if (shaderName == "MeshFillShader") {
    return EnumerateMeshFillReachable();
  }
  if (shaderName == "PerlinNoiseFillShader") {
    return EnumeratePerlinNoiseFillReachable();
  }
  if (shaderName == "TextureColorMatrixShader") {
    return EnumerateTextureColorMatrixReachable();
  }
  if (shaderName == "YUVTextureFillShader") {
    return EnumerateYUVTextureFillReachable();
  }
  if (shaderName == "DeviceSpaceTextureShader") {
    return EnumerateDeviceSpaceTextureReachable();
  }
  if (shaderName == "DeviceSpaceTexturedEffectShader") {
    return EnumerateDeviceSpaceTexturedEffectReachable();
  }
  if (shaderName == "ShapeInstancedTextureCoverageShader") {
    return EnumerateShapeInstancedTextureCoverageReachable();
  }
  if (shaderName == "ShapeInstancedFillShader") {
    return EnumerateShapeInstancedFillReachable();
  }
  if (shaderName == "TextureFillShader") {
    return EnumerateTextureFillReachable();
  }
  if (shaderName == "TiledTextureFillShader") {
    return EnumerateTiledTextureFillReachable();
  }
  if (shaderName == "QuadTextureFillShader") {
    return EnumerateQuadTextureFillReachable();
  }
  if (shaderName == "GaussianBlur1DShader") {
    return EnumerateGaussianBlur1DReachable();
  }
  if (shaderName == "TexturedEffectShader") {
    return EnumerateTexturedEffectReachable();
  }
  if (shaderName == "PointwiseTailShader") {
    return EnumeratePointwiseTailReachable();
  }
  if (shaderName == "PointwiseDirectShader") {
    return EnumeratePointwiseDirectReachable();
  }
  if (shaderName == "UnifiedGradientShader") {
    return EnumerateUnifiedGradientReachable();
  }
  if (shaderName == "AtlasTextFillShader") {
    return EnumerateAtlasTextFillReachable();
  }
  return std::nullopt;
}

}  // namespace tgfx
