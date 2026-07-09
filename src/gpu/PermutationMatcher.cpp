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
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/level1/ConstColorShader.h"
#include "gpu/shaders/level1/DeviceSpaceTextureShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gpu/shaders/level1/TiledTextureFillShader.h"

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
  // YUV textures require additional dimensions (Limited/Full range, I420/NV12 format) that are not
  // yet covered by the precompiled shader. Fall back to ProgramBuilder for YUV.
  if (te->isYUV()) {
    return std::nullopt;
  }
  auto vertDomain = TextureFillShader::D::domain();
  auto fragDomain = TextureFillShader::D::domain();
  std::vector<int> values(TextureFillShader::D::COUNT);
  values[TextureFillShader::D::HAS_YUV] = 0;
  values[TextureFillShader::D::ALPHA_ONLY] = te->isAlphaOnly() ? 1 : 0;
  values[TextureFillShader::D::HAS_RGBAAA] = te->hasRGBAAA() ? 1 : 0;
  values[TextureFillShader::D::HAS_SUBSET] = te->hasSubset() ? 1 : 0;
  auto vertIndex = vertDomain.encode(values);
  auto fragIndex = fragDomain.encode(values);
  return PermutationMatchResult{"TextureFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchTiledTextureFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TiledTextureEffect") {
    return std::nullopt;
  }
  auto* tte = static_cast<const TiledTextureEffect*>(fp);
  int modeX = 0;
  int modeY = 0;
  tte->getShaderModes(&modeX, &modeY);
  if (modeX == 0 && modeY == 0) {
    return std::nullopt;
  }
  using FD = TiledTextureFillShader::FragDims;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::SHADER_MODE_X] = modeX;
  fragValues[FD::SHADER_MODE_Y] = modeY;
  fragValues[FD::ALPHA_ONLY] = tte->isAlphaOnly() ? 1 : 0;
  fragValues[FD::HAS_STRICT] = tte->isStrict() ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);

  using VD = TiledTextureFillShader::Dims;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT);
  vertValues[VD::HAS_PERSPECTIVE] = tte->hasPerspective() ? 1 : 0;
  auto vertIndex = vertDomain.encode(vertValues);
  return PermutationMatchResult{"TiledTextureFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchConstColor(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "ConstColorProcessor") {
    return std::nullopt;
  }
  auto* ccp = static_cast<const ConstColorProcessor*>(fp);
  using FD = ConstColorShader::FragDims;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::INPUT_MODE] = ccp->getInputMode();
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"ConstColorShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchDeviceSpaceTexture(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "DeviceSpaceTextureEffect") {
    return std::nullopt;
  }
  auto* dste = static_cast<const DeviceSpaceTextureEffect*>(fp);
  using FD = DeviceSpaceTextureShader::Dims;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::ALPHA_ONLY] = dste->isAlphaOnly() ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"DeviceSpaceTextureShader", 0, fragIndex};
}

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo) {
  if (auto result = TryMatchTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchTiledTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchConstColor(programInfo)) {
    return result;
  }
  if (auto result = TryMatchDeviceSpaceTexture(programInfo)) {
    return result;
  }
  return std::nullopt;
}

}  // namespace tgfx
