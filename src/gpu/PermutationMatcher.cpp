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

#include "PermutationMatcher.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/level1/TextureFillShader.h"

namespace tgfx {

static std::optional<PermutationMatchResult> TryMatchTextureFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  // Only match when no custom XferProcessor is active. A non-empty XferProcessor changes the
  // shader structure (e.g. dst texture sampling) which is incompatible with precompiled variants.
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp);
  auto domain = TextureFillShader::D::domain();
  std::vector<int> values(TextureFillShader::D::COUNT);
  values[TextureFillShader::D::HAS_YUV] = te->isYUV() ? 1 : 0;
  values[TextureFillShader::D::ALPHA_ONLY] = te->isAlphaOnly() ? 1 : 0;
  values[TextureFillShader::D::HAS_RGBAAA] = te->hasRGBAAA() ? 1 : 0;
  values[TextureFillShader::D::HAS_SUBSET] = te->hasSubset() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"TextureFillShader", index, domain};
}

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo) {
  if (auto result = TryMatchTextureFill(programInfo)) {
    return result;
  }
  return std::nullopt;
}

}  // namespace tgfx
