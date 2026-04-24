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
#include <functional>
#include "gpu/ShaderMacroSet.h"

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

void GaussianBlur1DFragmentProcessor::BuildMacros(int maxSigma, ShaderMacroSet& macros) {
  macros.define("TGFX_BLUR_LOOP_LIMIT", 4 * maxSigma);
}

std::vector<ShaderVariant> GaussianBlur1DFragmentProcessor::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(static_cast<size_t>(MaxSupportedMaxSigma));
  std::hash<std::string> hasher;
  for (int i = 0; i < MaxSupportedMaxSigma; ++i) {
    int maxSigma = i + 1;
    ShaderMacroSet macros;
    BuildMacros(maxSigma, macros);
    ShaderVariant variant;
    variant.index = i;
    variant.name = std::string("GaussianBlur1D[maxSigma=") + std::to_string(maxSigma) + "]";
    variant.preamble = macros.toPreamble();
    variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
    variants.emplace_back(std::move(variant));
  }
  return variants;
}

}  // namespace tgfx
