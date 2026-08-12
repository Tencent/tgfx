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

#include "ClampedGradientEffect.h"
#include "gpu/AOTEffect.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/DualIntervalGradientColorizer.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureGradientColorizer.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"

namespace tgfx {
ClampedGradientEffect::ClampedGradientEffect(PlacementPtr<FragmentProcessor> colorizer,
                                             PlacementPtr<FragmentProcessor> gradLayout,
                                             Color leftBorderColor, Color rightBorderColor)
    : FragmentProcessor(ClassID()), leftBorderColor(leftBorderColor),
      rightBorderColor(rightBorderColor) {
  colorizerIndex = registerChildProcessor(std::move(colorizer));
  gradLayoutIndex = registerChildProcessor(std::move(gradLayout));
}

bool ClampedGradientEffect::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                       AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  auto* layoutFP = childProcessor(gradLayoutIndex);
  auto* colorizerFP = childProcessor(colorizerIndex);
  if (layoutFP == nullptr || colorizerFP == nullptr || layoutFP->numCoordTransforms() != 1) {
    return false;
  }
  AOTGradientParameters parameters = {};
  const auto& layoutName = layoutFP->name();
  if (layoutName == "LinearGradientLayout") {
    parameters.layoutType = 0;
  } else if (layoutName == "RadialGradientLayout") {
    parameters.layoutType = 1;
  } else if (layoutName == "ConicGradientLayout") {
    parameters.layoutType = 2;
    auto* conic = static_cast<const ConicGradientLayout*>(layoutFP);
    parameters.bias = conic->getBias();
    parameters.scale = conic->getScale();
  } else if (layoutName == "DiamondGradientLayout") {
    parameters.layoutType = 3;
  } else {
    return false;
  }
  auto* transform = layoutFP->coordTransform(0);
  parameters.coordMatrix = transform->matrix;
  parameters.hasPerspective = transform->matrix.hasPerspective();
  parameters.leftBorder = {leftBorderColor.red, leftBorderColor.green, leftBorderColor.blue,
                           leftBorderColor.alpha};
  parameters.rightBorder = {rightBorderColor.red, rightBorderColor.green, rightBorderColor.blue,
                            rightBorderColor.alpha};
  if (colorizerFP->name() == "SingleIntervalGradientColorizer") {
    auto* single = static_cast<const SingleIntervalGradientColorizer*>(colorizerFP);
    parameters.colorizerKind = 0;
    const auto& startColor = single->startColor();
    const auto& endColor = single->endColor();
    parameters.start = {startColor.red, startColor.green, startColor.blue, startColor.alpha};
    parameters.end = {endColor.red, endColor.green, endColor.blue, endColor.alpha};
  } else if (colorizerFP->name() == "DualIntervalGradientColorizer") {
    auto* dual = static_cast<const DualIntervalGradientColorizer*>(colorizerFP);
    parameters.colorizerKind = 1;
    const auto& scale01 = dual->getScale01();
    const auto& bias01 = dual->getBias01();
    const auto& scale23 = dual->getScale23();
    const auto& bias23 = dual->getBias23();
    parameters.scale01 = {scale01.red, scale01.green, scale01.blue, scale01.alpha};
    parameters.bias01 = {bias01.red, bias01.green, bias01.blue, bias01.alpha};
    parameters.scale23 = {scale23.red, scale23.green, scale23.blue, scale23.alpha};
    parameters.bias23 = {bias23.red, bias23.green, bias23.blue, bias23.alpha};
    parameters.threshold = dual->getThreshold();
  } else if (colorizerFP->name() == "UnrolledBinaryGradientColorizer") {
    auto* unrolled = static_cast<const UnrolledBinaryGradientColorizer*>(colorizerFP);
    parameters.colorizerKind = 2;
    parameters.intervalCount = unrolled->getIntervalCount();
    Color scales[8] = {};
    Color biases[8] = {};
    Rect thresholds1_7 = {};
    Rect thresholds9_13 = {};
    unrolled->getSegments(scales, biases, &thresholds1_7, &thresholds9_13);
    for (size_t index = 0; index < 8; ++index) {
      parameters.scales[index] = {scales[index].red, scales[index].green, scales[index].blue,
                                  scales[index].alpha};
      parameters.biases[index] = {biases[index].red, biases[index].green, biases[index].blue,
                                  biases[index].alpha};
    }
    parameters.thresholds1_7 = {thresholds1_7.left, thresholds1_7.top, thresholds1_7.right,
                                thresholds1_7.bottom};
    parameters.thresholds9_13 = {thresholds9_13.left, thresholds9_13.top, thresholds9_13.right,
                                 thresholds9_13.bottom};
  } else if (colorizerFP->name() == "TextureGradientColorizer") {
    // LUT colorizer: the baked gradient texture joins the chain as a sampler-only child; the
    // kernel's OP_GRADIENT LUT branch samples it at the computed t.
    auto* lut = static_cast<const TextureGradientColorizer*>(colorizerFP);
    if (lut->getGradient()->getTextureView() == nullptr) {
      return false;
    }
    parameters.colorizerKind = 3;
    parameters.lutProxy = lut->getGradient();
  } else {
    return false;
  }
  return builder->addGradientSource(input, parameters, output);
}
}  // namespace tgfx
