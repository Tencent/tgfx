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

#include "GaussianBlur1DFragmentProcessor.h"

namespace tgfx {

GaussianBlur1DFragmentProcessor::GaussianBlur1DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength, int maxSigma)
    : FragmentProcessor(ClassID()), sigma(sigma), direction(direction), stepLength(stepLength),
      maxSigma(maxSigma) {
  registerChildProcessor(std::move(processor));
}

void GaussianBlur1DFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(maxSigma);
}

}  // namespace tgfx
