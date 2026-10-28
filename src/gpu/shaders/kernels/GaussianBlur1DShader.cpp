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

#include "gpu/shaders/kernels/GaussianBlur1DShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"

namespace tgfx {

std::optional<GaussianBlur1DShader::Inputs> GaussianBlur1DShader::Extract(
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
  if (!IsProcessor<GaussianBlur1DFragmentProcessor>(fp)) {
    return std::nullopt;
  }
  auto* blur = static_cast<const GaussianBlur1DFragmentProcessor*>(fp);
  if (blur->numChildProcessors() != 1) {
    return std::nullopt;
  }
  if (!IsServableBlurChild(blur->childProcessor(0))) {
    return std::nullopt;
  }
  // The kernel radius is a runtime uniform, not a variant dimension. The precompiled shader uses
  // a fixed maximum kernel (MAX_SIGMA=9 -> radius up to 10) and breaks early at the runtime
  // radius, so any sigma in [1,10] maps to the same variant. Sigma > 10 exceeds the fixed kernel
  // and falls back.
  int sigma = blur->getMaxSigma();
  if (sigma < 1 || sigma > 10) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

}  // namespace tgfx
