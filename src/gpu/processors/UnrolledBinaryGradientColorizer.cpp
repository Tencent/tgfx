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

#include "UnrolledBinaryGradientColorizer.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"

namespace tgfx {
void UnrolledBinaryGradientColorizer::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(intervalCount));
}

void UnrolledBinaryGradientColorizer::BuildMacros(int intervalCount, ShaderMacroSet& macros) {
  macros.define("TGFX_UBGC_INTERVAL_COUNT", intervalCount);
}

std::vector<ShaderVariant> UnrolledBinaryGradientColorizer::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(static_cast<size_t>(MaxIntervalCount));
  std::hash<std::string> hasher;
  for (int i = 0; i < MaxIntervalCount; ++i) {
    int intervalCount = i + 1;
    ShaderMacroSet macros;
    BuildMacros(intervalCount, macros);
    ShaderVariant variant;
    variant.index = i;
    variant.name = std::string("UnrolledBinaryGradientColorizer[intervals=") +
                   std::to_string(intervalCount) + "]";
    variant.preamble = macros.toPreamble();
    variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
    variants.emplace_back(std::move(variant));
  }
  return variants;
}
}  // namespace tgfx
