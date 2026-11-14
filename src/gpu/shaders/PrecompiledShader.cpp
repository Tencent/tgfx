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

#include "gpu/shaders/PrecompiledShader.h"
#include <cstdint>
#include "gpu/shaders/Families.h"

namespace tgfx {

bool VariantUsesFramebufferFetch(uint16_t familyIndex, uint32_t ordinal) {
  const auto variant = DecodeVariant(familyIndex, ordinal);
  if (!variant.has_value()) {
    return false;
  }
  return XferDimValue(Families()[familyIndex], *variant) == XP_FRAMEBUFFER_FETCH;
}

bool PermutationCompilesForBackend(uint16_t familyIndex, uint32_t ordinal, ShaderProfile profile) {
  if (VariantUsesFramebufferFetch(familyIndex, ordinal) &&
      !ProfileSupportsFramebufferFetch(profile)) {
    return false;
  }
  return true;
}

}  // namespace tgfx
