/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
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

#include "BlendFormula.h"

namespace tgfx {

/**
 * When there is no coverage, or the blend mode can tweak alpha for coverage, we use the standard
 * Porter Duff formula.
 */
constexpr BlendFormula MakeCoeffFormula(BlendFactor srcCoeff, BlendFactor dstCoeff) {
  // When the coeffs are (Zero, Zero) or (Zero, One) we set the primary output to none.
  return (BlendFactor::Zero == srcCoeff &&
          (BlendFactor::Zero == dstCoeff || BlendFactor::One == dstCoeff))
             ? BlendFormula{BlendFormula::OutputType::None, BlendFormula::OutputType::None,
                            BlendOperation::Add, BlendFactor::Zero, dstCoeff}
             : BlendFormula{BlendFormula::OutputType::Modulate, BlendFormula::OutputType::None,
                            BlendOperation::Add, srcCoeff, dstCoeff};
}

/**
 * Basic coeff formula similar to MakeCoeffFormula but we will make the src f*Sa.
 */
constexpr BlendFormula MakeSAModulateFormula(BlendFactor srcCoeff, BlendFactor dstCoeff) {
  return {BlendFormula::OutputType::SAModulate, BlendFormula::OutputType::None, BlendOperation::Add,
          srcCoeff, dstCoeff};
}

/**
 * When there is coverage, the equation with f=coverage is:
 * D' = f * S * srcCoeff + D * (1 - [f * (1 - dstCoeff)])
 */
constexpr BlendFormula MakeCoverageFormula(BlendFormula::OutputType oneMinusDstCoeffModulateOutput,
                                           BlendFactor srcCoeff) {
  return {BlendFormula::OutputType::Modulate, oneMinusDstCoeffModulateOutput, BlendOperation::Add,
          srcCoeff, BlendFactor::OneMinusSrc1};
}

/**
 * When there is coverage and the src coeff is Zero.
 */
constexpr BlendFormula MakeCoverageSrcCoeffZeroFormula(
    BlendFormula::OutputType oneMinusDstCoeffModulateOutput) {
  return {oneMinusDstCoeffModulateOutput, BlendFormula::OutputType::None,
          BlendOperation::ReverseSubtract, BlendFactor::Dst, BlendFactor::One};
}

/**
 * When there is coverage and the dst coeff is Zero.
 */
constexpr BlendFormula MakeCoverageDstCoeffZeroFormula(BlendFactor srcCoeff) {
  return {BlendFormula::OutputType::Modulate, BlendFormula::OutputType::Coverage,
          BlendOperation::Add, srcCoeff, BlendFactor::OneMinusSrc1Alpha};
}

static constexpr BlendFormula Coeffs[2][15] = {
    {
        /* clear */ MakeCoeffFormula(BlendFactor::Zero, BlendFactor::Zero),
        /* src */ MakeCoeffFormula(BlendFactor::One, BlendFactor::Zero),
        /* dst */ MakeCoeffFormula(BlendFactor::Zero, BlendFactor::One),
        /* src-over */ MakeCoeffFormula(BlendFactor::One, BlendFactor::OneMinusSrcAlpha),
        /* dst-over */ MakeCoeffFormula(BlendFactor::OneMinusDstAlpha, BlendFactor::One),
        /* src-in */ MakeCoeffFormula(BlendFactor::DstAlpha, BlendFactor::Zero),
        /* dst-in */ MakeCoeffFormula(BlendFactor::Zero, BlendFactor::SrcAlpha),
        /* src-out */ MakeCoeffFormula(BlendFactor::OneMinusDstAlpha, BlendFactor::Zero),
        /* dst-out */ MakeCoeffFormula(BlendFactor::Zero, BlendFactor::OneMinusSrcAlpha),
        /* src-atop */ MakeCoeffFormula(BlendFactor::DstAlpha, BlendFactor::OneMinusSrcAlpha),
        /* dst-atop */ MakeCoeffFormula(BlendFactor::OneMinusDstAlpha, BlendFactor::SrcAlpha),
        /* xor */ MakeCoeffFormula(BlendFactor::OneMinusDstAlpha, BlendFactor::OneMinusSrcAlpha),
        /* plus */ MakeCoeffFormula(BlendFactor::One, BlendFactor::One),
        /* modulate */ MakeCoeffFormula(BlendFactor::Zero, BlendFactor::Src),
        /* screen */ MakeCoeffFormula(BlendFactor::One, BlendFactor::OneMinusSrc),
    },
    /*>> Has coverage, input color unknown <<*/ {
        /* clear */ MakeCoverageSrcCoeffZeroFormula(BlendFormula::OutputType::Coverage),
        /* src */ MakeCoverageDstCoeffZeroFormula(BlendFactor::One),
        /* dst */ MakeCoeffFormula(BlendFactor::Zero, BlendFactor::One),
        /* src-over */ MakeCoeffFormula(BlendFactor::One, BlendFactor::OneMinusSrcAlpha),
        /* dst-over */ MakeCoeffFormula(BlendFactor::OneMinusDstAlpha, BlendFactor::One),
        /* src-in */ MakeCoverageDstCoeffZeroFormula(BlendFactor::DstAlpha),
        /* dst-in */ MakeCoverageSrcCoeffZeroFormula(BlendFormula::OutputType::ISAModulate),
        /* src-out */ MakeCoverageDstCoeffZeroFormula(BlendFactor::OneMinusDstAlpha),
        /* dst-out */ MakeCoeffFormula(BlendFactor::Zero, BlendFactor::OneMinusSrcAlpha),
        /* src-atop */ MakeCoeffFormula(BlendFactor::DstAlpha, BlendFactor::OneMinusSrcAlpha),
        /* dst-atop */
        MakeCoverageFormula(BlendFormula::OutputType::ISAModulate, BlendFactor::OneMinusDstAlpha),
        /* xor */ MakeCoeffFormula(BlendFactor::OneMinusDstAlpha, BlendFactor::OneMinusSrcAlpha),
        /* plus */ MakeCoeffFormula(BlendFactor::One, BlendFactor::One),
        /* modulate */ MakeCoverageSrcCoeffZeroFormula(BlendFormula::OutputType::ISCModulate),
        /* screen */ MakeCoeffFormula(BlendFactor::One, BlendFactor::OneMinusSrc),
    }};

bool BlendModeAsCoeff(BlendMode mode, bool hasCoverage, BlendFormula* blendFormula) {
  if (mode > BlendMode::Screen) {
    return false;
  }
  if (blendFormula != nullptr) {
    const auto& formula = Coeffs[hasCoverage][static_cast<int>(mode)];
    *blendFormula = formula;
  }
  return true;
}

bool BlendModeIsOpaque(BlendMode mode, OpacityType srcColorOpacity) {
  BlendFormula blendFormula = {};
  if (!BlendModeAsCoeff(mode, false, &blendFormula)) {
    return false;
  }
  switch (blendFormula.srcFactor()) {
    case BlendFactor::Zero:
    case BlendFactor::DstAlpha:
    case BlendFactor::Dst:
    case BlendFactor::OneMinusDstAlpha:
    case BlendFactor::OneMinusDst:
      return false;
    default:
      break;
  }
  switch (blendFormula.dstFactor()) {
    case BlendFactor::Zero:
      return true;
    case BlendFactor::OneMinusSrcAlpha:
      return srcColorOpacity == OpacityType::Opaque;
    case BlendFactor::SrcAlpha:
      return srcColorOpacity == OpacityType::TransparentBlack ||
             srcColorOpacity == OpacityType::TransparentAlpha;
    case BlendFactor::Src:
      return srcColorOpacity == OpacityType::TransparentBlack;
    default:
      return false;
  }
}

bool BlendModeNeedDstTexture(BlendMode mode, bool hasCoverage) {
  if (mode == BlendMode::SrcOver) {
    return false;
  }
  BlendFormula formula;
  if (!BlendModeAsCoeff(mode, hasCoverage, &formula) || formula.needSecondaryOutput() ||
      formula.primaryOutputType() != BlendFormula::OutputType::Modulate) {
    return true;
  }
  return false;
}

}  // namespace tgfx
