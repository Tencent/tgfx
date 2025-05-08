/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "Blend.h"

namespace tgfx {

/**
 * When there is no coverage, or the blend mode can tweak alpha for coverage, we use the standard
 * Porter Duff formula.
 */
constexpr BlendFormula MakeCoeffFormula(BlendModeCoeff srcCoeff, BlendModeCoeff dstCoeff) {
  // When the coeffs are (Zero, Zero) or (Zero, One) we set the primary output to none.
  return (BlendModeCoeff::Zero == srcCoeff &&
          (BlendModeCoeff::Zero == dstCoeff || BlendModeCoeff::One == dstCoeff))
             ? BlendFormula{BlendFormula::OutputType::None, BlendFormula::OutputType::None,
                            BlendEquation::Add, BlendModeCoeff::Zero, dstCoeff}
             : BlendFormula{BlendFormula::OutputType::Modulate, BlendFormula::OutputType::None,
                            BlendEquation::Add, srcCoeff, dstCoeff};
}

/**
 * Basic coeff formula similar to MakeCoeffFormula but we will make the src f*Sa.
 */
constexpr BlendFormula MakeSAModulateFormula(BlendModeCoeff srcCoeff, BlendModeCoeff dstCoeff) {
  return {BlendFormula::OutputType::SAModulate, BlendFormula::OutputType::None, BlendEquation::Add,
          srcCoeff, dstCoeff};
}

/**
 * When there is coverage, the equation with f=coverage is:
 * D' = f * S * srcCoeff + D * (1 - [f * (1 - dstCoeff)])
 */
constexpr BlendFormula MakeCoverageFormula(BlendFormula::OutputType oneMinusDstCoeffModulateOutput,
                                           BlendModeCoeff srcCoeff) {
  return {BlendFormula::OutputType::Modulate, oneMinusDstCoeffModulateOutput, BlendEquation::Add,
          srcCoeff, BlendModeCoeff::IS2C};
}

/**
 * When there is coverage and the src coeff is Zero.
 */
constexpr BlendFormula MakeCoverageSrcCoeffZeroFormula(
    BlendFormula::OutputType oneMinusDstCoeffModulateOutput) {
  return {oneMinusDstCoeffModulateOutput, BlendFormula::OutputType::None,
          BlendEquation::ReverseSubtract, BlendModeCoeff::DC, BlendModeCoeff::One};
}

/**
 * When there is coverage and the dst coeff is Zero.
 */
constexpr BlendFormula MakeCoverageDstCoeffZeroFormula(BlendModeCoeff srcCoeff) {
  return {BlendFormula::OutputType::Modulate, BlendFormula::OutputType::Coverage,
          BlendEquation::Add, srcCoeff, BlendModeCoeff::IS2A};
}

static constexpr BlendFormula Coeffs[2][15] = {
    {
        /* clear */ MakeCoeffFormula(BlendModeCoeff::Zero, BlendModeCoeff::Zero),
        /* src */ MakeCoeffFormula(BlendModeCoeff::One, BlendModeCoeff::Zero),
        /* dst */ MakeCoeffFormula(BlendModeCoeff::Zero, BlendModeCoeff::One),
        /* src-over */ MakeCoeffFormula(BlendModeCoeff::One, BlendModeCoeff::ISA),
        /* dst-over */ MakeCoeffFormula(BlendModeCoeff::IDA, BlendModeCoeff::One),
        /* src-in */ MakeCoeffFormula(BlendModeCoeff::DA, BlendModeCoeff::Zero),
        /* dst-in */ MakeCoeffFormula(BlendModeCoeff::Zero, BlendModeCoeff::SA),
        /* src-out */ MakeCoeffFormula(BlendModeCoeff::IDA, BlendModeCoeff::Zero),
        /* dst-out */ MakeCoeffFormula(BlendModeCoeff::Zero, BlendModeCoeff::ISA),
        /* src-atop */ MakeCoeffFormula(BlendModeCoeff::DA, BlendModeCoeff::ISA),
        /* dst-atop */ MakeCoeffFormula(BlendModeCoeff::IDA, BlendModeCoeff::SA),
        /* xor */ MakeCoeffFormula(BlendModeCoeff::IDA, BlendModeCoeff::ISA),
        /* plus */ MakeCoeffFormula(BlendModeCoeff::One, BlendModeCoeff::One),
        /* modulate */ MakeCoeffFormula(BlendModeCoeff::Zero, BlendModeCoeff::SC),
        /* screen */ MakeCoeffFormula(BlendModeCoeff::One, BlendModeCoeff::ISC),
    },
    /*>> Has coverage, input color unknown <<*/ {
        /* clear */ MakeCoverageSrcCoeffZeroFormula(BlendFormula::OutputType::Coverage),
        /* src */ MakeCoverageDstCoeffZeroFormula(BlendModeCoeff::One),
        /* dst */ MakeCoeffFormula(BlendModeCoeff::Zero, BlendModeCoeff::One),
        /* src-over */ MakeCoeffFormula(BlendModeCoeff::One, BlendModeCoeff::ISA),
        /* dst-over */ MakeCoeffFormula(BlendModeCoeff::IDA, BlendModeCoeff::One),
        /* src-in */ MakeCoverageDstCoeffZeroFormula(BlendModeCoeff::DA),
        /* dst-in */ MakeCoverageSrcCoeffZeroFormula(BlendFormula::OutputType::ISAModulate),
        /* src-out */ MakeCoverageDstCoeffZeroFormula(BlendModeCoeff::IDA),
        /* dst-out */ MakeCoeffFormula(BlendModeCoeff::Zero, BlendModeCoeff::ISA),
        /* src-atop */ MakeCoeffFormula(BlendModeCoeff::DA, BlendModeCoeff::ISA),
        /* dst-atop */
        MakeCoverageFormula(BlendFormula::OutputType::ISAModulate, BlendModeCoeff::IDA),
        /* xor */ MakeCoeffFormula(BlendModeCoeff::IDA, BlendModeCoeff::ISA),
        /* plus */ MakeCoeffFormula(BlendModeCoeff::One, BlendModeCoeff::One),
        /* modulate */ MakeCoverageSrcCoeffZeroFormula(BlendFormula::OutputType::ISCModulate),
        /* screen */ MakeCoeffFormula(BlendModeCoeff::One, BlendModeCoeff::ISC),
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
  switch (blendFormula.srcCoeff()) {
    case BlendModeCoeff::Zero:
    case BlendModeCoeff::DA:
    case BlendModeCoeff::DC:
    case BlendModeCoeff::IDA:
    case BlendModeCoeff::IDC:
      return false;
    default:
      break;
  }
  switch (blendFormula.dstCoeff()) {
    case BlendModeCoeff::Zero:
      return true;
    case BlendModeCoeff::ISA:
      return srcColorOpacity == OpacityType::Opaque;
    case BlendModeCoeff::SA:
      return srcColorOpacity == OpacityType::TransparentBlack ||
             srcColorOpacity == OpacityType::TransparentAlpha;
    case BlendModeCoeff::SC:
      return srcColorOpacity == OpacityType::TransparentBlack;
    default:
      return false;
  }
}
}  // namespace tgfx
