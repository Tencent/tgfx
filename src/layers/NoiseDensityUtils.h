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

#pragma once

#include <utility>
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

int ComputeLinearBucket(float density, float slope, float intercept);

// Slopes/intercepts are chosen so that at density=0 each band collapses to a single bucket
// (dark=25, bright=74) and at density=1 the bands fully expand without overlap:
//   dark  -> [0, 49], bright -> [50, 99].
// This keeps the transition strictly monotonic across the full 0..1 range, avoiding the
// plateau that occurs when bucket values are clamped at the 0 / 99 boundaries.
std::pair<int, int> ComputeDarkBandBuckets(float density);

std::pair<int, int> ComputeBrightBandBuckets(float density);

std::shared_ptr<ColorFilter> MakeLumaAlphaThresholdFilter(float threshold);

std::shared_ptr<Shader> MakeDensityBandShader(std::shared_ptr<Shader> noiseShader, int lowerBucket,
                                              int upperBucket);

std::shared_ptr<Shader> MakeBrightDensityFilter(std::shared_ptr<Shader> noiseShader, float density);

std::shared_ptr<Shader> MakeDarkDensityFilter(std::shared_ptr<Shader> noiseShader, float density);

}  // namespace tgfx
