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

#pragma once

#include <cstdint>
#include <cstring>
#include "gpu/BlendFactor.h"
#include "gpu/BlendOperation.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {
struct BlendFormula {
  /**
    * Values the shader can write to primary and secondary outputs. These are all modulated by
    * coverage. We will ignore the multiplies when not using coverage.
    */
  enum class OutputType {
    None,         //<! 0
    Coverage,     //<! inputCoverage
    Modulate,     //<! inputColor * inputCoverage
    SAModulate,   //<! inputColor.a * inputCoverage
    ISAModulate,  //<! (1 - inputColor.a) * inputCoverage
    ISCModulate,  //<! (1 - inputColor) * inputCoverage
  };

  BlendFormula() {
    // default to src-over blendmode
    bitFields.equation = static_cast<uint8_t>(BlendOperation::Add);
    bitFields.srcCoeff = static_cast<uint8_t>(BlendFactor::One);
    bitFields.dstCoeff = static_cast<uint8_t>(BlendFactor::OneMinusSrcAlpha);
    bitFields.primaryOutputType = static_cast<uint8_t>(OutputType::Modulate);
    bitFields.secondaryOutputType = static_cast<uint8_t>(OutputType::None);
  }

  constexpr BlendFormula(OutputType primaryOutputType, OutputType secondaryOutputType,
                         BlendOperation eq, BlendFactor src, BlendFactor dst) {
    bitFields.primaryOutputType = static_cast<uint8_t>(primaryOutputType);
    bitFields.secondaryOutputType = static_cast<uint8_t>(secondaryOutputType);
    bitFields.equation = static_cast<uint8_t>(eq);
    bitFields.srcCoeff = static_cast<uint8_t>(src);
    bitFields.dstCoeff = static_cast<uint8_t>(dst);
  }

  bool needSecondaryOutput() const {
    return static_cast<OutputType>(bitFields.secondaryOutputType) != OutputType::None;
  }

  OutputType primaryOutputType() const {
    return static_cast<OutputType>(bitFields.primaryOutputType);
  }

  OutputType secondaryOutputType() const {
    return static_cast<OutputType>(bitFields.secondaryOutputType);
  }

  BlendOperation operation() const {
    return static_cast<BlendOperation>(bitFields.equation);
  }

  BlendFactor srcFactor() const {
    return static_cast<BlendFactor>(bitFields.srcCoeff);
  }

  BlendFactor dstFactor() const {
    return static_cast<BlendFactor>(bitFields.dstCoeff);
  }

 private:
  struct {
    uint8_t primaryOutputType : 3;
    uint8_t secondaryOutputType : 3;
    uint8_t srcCoeff : 4;
    uint8_t dstCoeff : 4;
    uint8_t equation : 2;
  } bitFields = {};
};

/**
 * Determines if the given blend mode is coefficient-based and retrieves the blend formula if so.
 */
bool BlendModeAsCoeff(BlendMode mode, bool hasCoverage = false,
                      BlendFormula* blendFormula = nullptr);

enum class OpacityType {
  // The opacity is unknown
  Unknown,
  // The src color is known to be opaque (alpha == 255)
  Opaque,
  // The src color is known to be fully transparent (color == 0)
  TransparentBlack,
  // The src alpha is known to be fully transparent (alpha == 0)
  TransparentAlpha,
};

/**
 * Returns true if 'mode' is opaque given the src color opacity.
 */
bool BlendModeIsOpaque(BlendMode mode, OpacityType srcColorOpacity);

/**
 * Returns true if the blend mode needs a destination texture.
 */
bool BlendModeNeedDstTexture(BlendMode mode, bool hasCoverage);

}  // namespace tgfx
