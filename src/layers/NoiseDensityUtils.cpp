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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "layers/NoiseDensityUtils.h"

namespace tgfx {

int ComputeLinearBucket(float density, float slope, float intercept) {
  density = std::max(0.0f, std::min(1.0f, density));
  auto value = slope * density + intercept;
  auto bucket = static_cast<int>(value + 0.5f);
  return std::max(0, std::min(99, bucket));
}

std::pair<int, int> ComputeDarkBandBuckets(float density) {
  auto lower = ComputeLinearBucket(density, -25.0f, 25.0f);
  auto upper = ComputeLinearBucket(density, 24.0f, 25.0f);
  return {lower, upper};
}

std::pair<int, int> ComputeBrightBandBuckets(float density) {
  auto lower = ComputeLinearBucket(density, -24.0f, 74.0f);
  auto upper = ComputeLinearBucket(density, 25.0f, 74.0f);
  return {lower, upper};
}

std::shared_ptr<ColorFilter> MakeLumaAlphaThresholdFilter(float threshold) {
  if (threshold <= 0.0f) {
    return nullptr;
  }
  // clang-format off
  std::array<float, 20> lumaMatrix = {
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f,
  };
  // clang-format on
  auto lumaFilter = ColorFilter::Matrix(lumaMatrix);
  auto thresholdFilter = ColorFilter::AlphaThreshold(threshold);
  return ColorFilter::Compose(lumaFilter, thresholdFilter);
}

std::shared_ptr<Shader> MakeDensityBandShader(std::shared_ptr<Shader> noiseShader, int lowerBucket,
                                              int upperBucket) {
  if (noiseShader == nullptr || lowerBucket < 0 || upperBucket < lowerBucket) {
    return nullptr;
  }
  auto lowerThreshold = static_cast<float>(std::max(0, std::min(99, lowerBucket))) / 100.0f;
  auto upperThreshold = static_cast<float>(std::max(0, std::min(100, upperBucket + 1))) / 100.0f;
  auto lowerMaskFilter = MakeLumaAlphaThresholdFilter(lowerThreshold);
  if (upperThreshold >= 1.0f) {
    if (lowerMaskFilter == nullptr) {
      return Shader::MakeColorShader(Color::White());
    }
    return noiseShader->makeWithColorFilter(std::move(lowerMaskFilter));
  }
  auto upperMaskFilter = MakeLumaAlphaThresholdFilter(upperThreshold);
  auto upperMask = noiseShader->makeWithColorFilter(std::move(upperMaskFilter));
  if (lowerMaskFilter == nullptr) {
    return Shader::MakeBlend(BlendMode::DstOut, Shader::MakeColorShader(Color::Black()),
                             std::move(upperMask));
  }
  auto lowerMask = noiseShader->makeWithColorFilter(std::move(lowerMaskFilter));
  return Shader::MakeBlend(BlendMode::DstOut, std::move(lowerMask), std::move(upperMask));
}

std::shared_ptr<Shader> MakeBrightDensityFilter(std::shared_ptr<Shader> noiseShader,
                                                float density) {
  auto buckets = ComputeBrightBandBuckets(density);
  return MakeDensityBandShader(std::move(noiseShader), buckets.first, buckets.second);
}

std::shared_ptr<Shader> MakeDarkDensityFilter(std::shared_ptr<Shader> noiseShader, float density) {
  auto buckets = ComputeDarkBandBuckets(density);
  return MakeDensityBandShader(std::move(noiseShader), buckets.first, buckets.second);
}

}  // namespace tgfx
