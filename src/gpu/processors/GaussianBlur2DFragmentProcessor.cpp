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

#include "GaussianBlur2DFragmentProcessor.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"

namespace tgfx {

GaussianBlur2DFragmentProcessor::GaussianBlur2DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigmaX, float sigmaY, const Point& origin)
    : FragmentProcessor(ClassID()), origin(origin) {
  registerChildProcessor(std::move(processor));
  // The same half-kernels as the separable blur, so the product weights sum to one as well.
  radiusX = GaussianBlur1DFragmentProcessor::ComputeHalfKernel(sigmaX, kernelX.data(), MAX_RADIUS);
  radiusY = GaussianBlur1DFragmentProcessor::ComputeHalfKernel(sigmaY, kernelY.data(), MAX_RADIUS);
}

}  // namespace tgfx
