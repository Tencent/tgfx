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
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"
#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include "gpu/shaders/level1/ConstColorShader.h"
#include "gpu/shaders/level1/DeviceSpaceTextureShader.h"
#include "gpu/shaders/level1/GradientFillShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/TextureClipShader.h"
#include "gpu/shaders/level1/TextureColorMatrixShader.h"
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

static std::optional<PermutationMatchResult> TryMatchQuadTextureFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  // Bail on unsupported variants — fall back to ProgramBuilder.
  if (!quadGP->hasCommonColor()) {
    return std::nullopt;
  }
  if (quadGP->getHasUVPerspective()) {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp);
  if (te->isYUV()) {
    return std::nullopt;
  }

  using VD = QuadTextureFillShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  vertValues[VD::HAS_UV_COORD] = !quadGP->hasUVMatrix() ? 1 : 0;
  vertValues[VD::HAS_SUBSET] = quadGP->getHasSubset() ? 1 : 0;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = QuadTextureFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::ALPHA_ONLY] = te->isAlphaOnly() ? 1 : 0;
  fragValues[FD::HAS_RGBAAA] = te->hasRGBAAA() ? 1 : 0;
  fragValues[FD::HAS_SUBSET] = te->hasSubset() ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"QuadTextureFillShader", vertIndex, fragIndex};
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

static int GradientLayoutTypeIndex(const std::string& layoutName) {
  if (layoutName == "LinearGradientLayout") {
    return 0;
  }
  if (layoutName == "RadialGradientLayout") {
    return 1;
  }
  if (layoutName == "ConicGradientLayout") {
    return 2;
  }
  if (layoutName == "DiamondGradientLayout") {
    return 3;
  }
  return -1;
}

static std::optional<PermutationMatchResult> TryMatchGradientFill(const ProgramInfo* programInfo) {
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
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "UnrolledBinaryGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  // Bail out on perspective transforms — not covered by precompiled variants.
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }
  auto* ubgc = static_cast<const UnrolledBinaryGradientColorizer*>(colorizer);
  int intervalCount = ubgc->getIntervalCount();
  if (intervalCount < 1 || intervalCount > 8) {
    return std::nullopt;
  }

  using D = GradientFillShader::Dims;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT);
  fragValues[D::LAYOUT_TYPE] = layoutType;
  fragValues[D::INTERVAL_COUNT] = intervalCount - 1;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"GradientFillShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchTextureColorMatrix(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 2) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp0 = programInfo->getFragmentProcessor(0);
  auto fp1 = programInfo->getFragmentProcessor(1);
  if (fp0->name() != "TextureEffect" || fp1->name() != "ColorMatrixFragmentProcessor") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp0);
  if (te->isYUV()) {
    return std::nullopt;
  }
  using D = TextureColorMatrixShader::D;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT);
  fragValues[D::ALPHA_ONLY] = te->isAlphaOnly() ? 1 : 0;
  fragValues[D::HAS_RGBAAA] = te->hasRGBAAA() ? 1 : 0;
  fragValues[D::HAS_SUBSET] = te->hasSubset() ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"TextureColorMatrixShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchTextureClip(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 2) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp0 = programInfo->getFragmentProcessor(0);
  auto fp1 = programInfo->getFragmentProcessor(1);
  if (fp0->name() != "TextureEffect" || fp1->name() != "AARectEffect") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp0);
  if (te->isYUV()) {
    return std::nullopt;
  }
  using D = TextureClipShader::D;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT);
  fragValues[D::ALPHA_ONLY] = te->isAlphaOnly() ? 1 : 0;
  fragValues[D::HAS_RGBAAA] = te->hasRGBAAA() ? 1 : 0;
  fragValues[D::HAS_SUBSET] = te->hasSubset() ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"TextureClipShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchAtlasTextFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "AtlasTextGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* atgp = static_cast<const AtlasTextGeometryProcessor*>(gp);
  using D = AtlasTextFillShader::D;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COVERAGE] = atgp->getAAType() == AAType::Coverage ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = atgp->hasCommonColor() ? 1 : 0;
  values[D::ALPHA_ONLY] = atgp->isAlphaOnly() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"AtlasTextFillShader", index, index};
}

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo) {
  if (auto result = TryMatchTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchQuadTextureFill(programInfo)) {
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
  if (auto result = TryMatchGradientFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchTextureColorMatrix(programInfo)) {
    return result;
  }
  if (auto result = TryMatchTextureClip(programInfo)) {
    return result;
  }
  if (auto result = TryMatchAtlasTextFill(programInfo)) {
    return result;
  }
  return std::nullopt;
}

}  // namespace tgfx
