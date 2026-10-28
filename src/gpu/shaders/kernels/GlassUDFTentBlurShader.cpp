/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/GlassUDFTentBlurShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"

namespace tgfx {

std::optional<GlassUDFTentBlurShader::Inputs> GlassUDFTentBlurShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedDeviceMaskValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (!IsProcessor<GlassUDFTentBlurFragmentProcessor>(fp)) {
    return std::nullopt;
  }
  auto* tent = static_cast<const GlassUDFTentBlurFragmentProcessor*>(fp);
  if (tent->numChildProcessors() != 1) {
    return std::nullopt;
  }
  // The horizontal pass reads a plain TextureEffect; the vertical pass a TiledTextureEffect.
  if (!IsServableBlurChild(tent->childProcessor(0))) {
    return std::nullopt;
  }
  // The loop upper bound is the fixed constant 64; a longer max radius exceeds it and falls back.
  if (tent->getMaxRadius() > 64) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

}  // namespace tgfx
