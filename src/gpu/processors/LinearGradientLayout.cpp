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

#include "LinearGradientLayout.h"
#include <functional>
#include "gpu/ShaderMacroSet.h"

namespace tgfx {
LinearGradientLayout::LinearGradientLayout(Matrix matrix)
    : FragmentProcessor(ClassID()), coordTransform(matrix) {
  addCoordTransform(&coordTransform);
}

void LinearGradientLayout::onComputeProcessorKey(BytesKey* bytesKey) const {
  const uint32_t flags = coordTransform.matrix.hasPerspective() ? 1 : 0;
  bytesKey->write(flags);
}

void LinearGradientLayout::BuildMacros(bool hasPerspective, ShaderMacroSet& macros) {
  if (hasPerspective) {
    macros.define("TGFX_LGRAD_PERSPECTIVE");
  }
}

std::vector<ShaderVariant> LinearGradientLayout::EnumerateVariants() {
  std::vector<ShaderVariant> variants;
  variants.reserve(2);
  std::hash<std::string> hasher;
  for (int i = 0; i < 2; ++i) {
    ShaderMacroSet macros;
    BuildMacros(i != 0, macros);
    ShaderVariant variant;
    variant.index = i;
    variant.name = std::string("LinearGradientLayout[perspective=") + (i ? "1" : "0") + "]";
    variant.preamble = macros.toPreamble();
    variant.runtimeKeyHash = static_cast<uint64_t>(hasher(variant.preamble));
    variants.emplace_back(std::move(variant));
  }
  return variants;
}
}  // namespace tgfx
