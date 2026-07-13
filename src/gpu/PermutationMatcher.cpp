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
#include <skcms.h>
#include "gpu/Swizzle.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/ComplexEllipseGeometryProcessor.h"
#include "gpu/processors/ComplexNonAARRectGeometryProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/DualIntervalGradientColorizer.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TextureGradientColorizer.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/shaders/level1/AlphaThresholdShader.h"
#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include "gpu/shaders/level1/BlendMergeShader.h"
#include "gpu/shaders/level1/ColorSpaceXformShader.h"
#include "gpu/shaders/level1/ComplexEllipseFillShader.h"
#include "gpu/shaders/level1/ComplexNonAARRectFillShader.h"
#include "gpu/shaders/level1/ConstColorShader.h"
#include "gpu/shaders/level1/DeviceSpaceTextureShader.h"
#include "gpu/shaders/level1/DualIntervalGradientShader.h"
#include "gpu/shaders/level1/EllipseFillShader.h"
#include "gpu/shaders/level1/GaussianBlur1DShader.h"
#include "gpu/shaders/level1/GradientFillShader.h"
#include "gpu/shaders/level1/HairlineLineShader.h"
#include "gpu/shaders/level1/HairlineQuadShader.h"
#include "gpu/shaders/level1/LumaShader.h"
#include "gpu/shaders/level1/MeshFillShader.h"
#include "gpu/shaders/level1/NonAARRectFillShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/SingleIntervalGradientShader.h"
#include "gpu/shaders/level1/TextureClipShader.h"
#include "gpu/shaders/level1/TextureColorMatrixShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gpu/shaders/level1/TextureGradientShader.h"
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
  if (te->numTextureSamplers() == 0) {
    return std::nullopt;
  }
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
  if (tte->hasPerspective()) {
    return std::nullopt;
  }
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
  return PermutationMatchResult{"TiledTextureFillShader", 0, fragIndex};
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

static std::optional<PermutationMatchResult> TryMatchQuadColorFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  using D = QuadColorFillShader::D;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT, 0);
  values[D::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  values[D::HAS_COLOR] = !quadGP->hasCommonColor() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"QuadColorFillShader", index, index};
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
  if (te->numTextureSamplers() == 0) {
    return std::nullopt;
  }
  if (te->isYUV()) {
    return std::nullopt;
  }
  // Vertex and fragment subset flags must agree: the vertex shader outputs a subset varying only
  // when HAS_SUBSET is set, and the fragment shader expects to read it only when its own
  // HAS_SUBSET is set. A mismatch causes a pipeline creation failure or GPU hang.
  bool gpSubset = quadGP->getHasSubset();
  bool teSubset = te->hasSubset();
  if (gpSubset != teSubset) {
    return std::nullopt;
  }

  using VD = QuadTextureFillShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  vertValues[VD::HAS_UV_COORD] = !quadGP->hasUVMatrix() ? 1 : 0;
  vertValues[VD::HAS_SUBSET] = gpSubset ? 1 : 0;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = QuadTextureFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::ALPHA_ONLY] = te->isAlphaOnly() ? 1 : 0;
  fragValues[FD::HAS_RGBAAA] = te->hasRGBAAA() ? 1 : 0;
  fragValues[FD::HAS_SUBSET] = teSubset ? 1 : 0;
  fragValues[FD::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  fragValues[FD::HAS_COLOR] = !quadGP->hasCommonColor() ? 1 : 0;
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

static std::optional<PermutationMatchResult> TryMatchSingleIntervalGradient(
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
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "SingleIntervalGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }

  using D = SingleIntervalGradientShader::Dims;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT);
  fragValues[D::LAYOUT_TYPE] = layoutType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"SingleIntervalGradientShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchDualIntervalGradient(
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
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "DualIntervalGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }

  using D = DualIntervalGradientShader::Dims;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT);
  fragValues[D::LAYOUT_TYPE] = layoutType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"DualIntervalGradientShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchTextureGradient(
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
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "TextureGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }

  using D = TextureGradientShader::Dims;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT);
  fragValues[D::LAYOUT_TYPE] = layoutType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"TextureGradientShader", 0, fragIndex};
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

static int GetGPType(const GeometryProcessor* gp) {
  auto name = gp->name();
  if (name == "DefaultGeometryProcessor") {
    return 0;
  }
  if (name == "QuadPerEdgeAAGeometryProcessor") {
    return 1;
  }
  return -1;
}

static std::optional<PermutationMatchResult> TryMatchAlphaThreshold(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "AlphaStepFragmentProcessor") {
    return std::nullopt;
  }
  using D = AlphaThresholdShader::Dims;
  auto vertDomain = D::domain();
  std::vector<int> vertValues(D::COUNT);
  vertValues[D::GP_TYPE] = gpType;
  auto vertIndex = vertDomain.encode(vertValues);
  return PermutationMatchResult{"AlphaThresholdShader", vertIndex, 0};
}

static std::optional<PermutationMatchResult> TryMatchLuma(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "LumaFragmentProcessor") {
    return std::nullopt;
  }
  using D = LumaShader::Dims;
  auto vertDomain = D::domain();
  std::vector<int> vertValues(D::COUNT);
  vertValues[D::GP_TYPE] = gpType;
  auto vertIndex = vertDomain.encode(vertValues);
  return PermutationMatchResult{"LumaShader", vertIndex, 0};
}

static int TFTypeToIndex(gfx::skcms_TFType type) {
  switch (type) {
    case gfx::skcms_TFType_sRGBish:
      return 0;
    case gfx::skcms_TFType_PQish:
      return 1;
    case gfx::skcms_TFType_HLGish:
      return 2;
    case gfx::skcms_TFType_HLGinvish:
      return 3;
    default:
      return -1;
  }
}

static std::optional<PermutationMatchResult> TryMatchColorSpaceXform(
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
  if (fp->name() != "ColorSpaceXformEffect") {
    return std::nullopt;
  }
  auto* cse = static_cast<const ColorSpaceXformEffect*>(fp);
  auto* xform = cse->colorXform();
  if (!xform) {
    return std::nullopt;
  }

  using FD = ColorSpaceXformShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::UNPREMUL] = xform->flags.unPremul ? 1 : 0;
  fragValues[FD::LINEARIZE] = xform->flags.linearize ? 1 : 0;
  fragValues[FD::SRC_OOTF] = xform->flags.srcOOTF ? 1 : 0;
  fragValues[FD::GAMUT_TRANSFORM] = xform->flags.gamutTransform ? 1 : 0;
  fragValues[FD::DST_OOTF] = xform->flags.dstOOTF ? 1 : 0;
  fragValues[FD::ENCODE] = xform->flags.encode ? 1 : 0;
  fragValues[FD::PREMUL] = xform->flags.premul ? 1 : 0;

  if (xform->flags.linearize) {
    int srcIdx = TFTypeToIndex(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->srcTransferFunction)));
    if (srcIdx < 0) {
      return std::nullopt;
    }
    fragValues[FD::SRC_TF_TYPE] = srcIdx;
  }
  if (xform->flags.encode) {
    int dstIdx = TFTypeToIndex(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->dstTransferFunctionInverse)));
    if (dstIdx < 0) {
      return std::nullopt;
    }
    fragValues[FD::DST_TF_TYPE] = dstIdx;
  }

  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"ColorSpaceXformShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchGaussianBlur1D(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "GaussianBlur1DFragmentProcessor") {
    return std::nullopt;
  }
  auto* blur = static_cast<const GaussianBlur1DFragmentProcessor*>(fp);
  // The precompiled blur shader inlines a simple texture() call for its child.
  // It does NOT support TiledTextureEffect children (which need Subset/Clamp/Dimension uniforms).
  if (blur->numChildProcessors() != 1) {
    return std::nullopt;
  }
  auto childFP = blur->childProcessor(0);
  if (childFP == nullptr || childFP->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* childTE = static_cast<const TextureEffect*>(childFP);
  if (childTE->numTextureSamplers() == 0 || childTE->hasSubset()) {
    return std::nullopt;
  }
  int sigma = blur->getMaxSigma();
  if (sigma < 1 || sigma > 10) {
    return std::nullopt;
  }
  using VD = GaussianBlur1DShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT);
  vertValues[VD::GP_TYPE] = gpType;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = GaussianBlur1DShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::MAX_SIGMA] = sigma - 1;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"GaussianBlur1DShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchBlendMerge(const ProgramInfo* programInfo) {
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
  auto fpName = fp->name();
  int childType = -1;
  if (fpName == "XfermodeFragmentProcessor - dst") {
    childType = 0;
  } else if (fpName == "XfermodeFragmentProcessor - src") {
    childType = 1;
  } else if (fpName == "XfermodeFragmentProcessor - two") {
    childType = 2;
  } else {
    return std::nullopt;
  }
  auto* xfp = static_cast<const XfermodeFragmentProcessor*>(fp);
  int blendMode = static_cast<int>(xfp->getMode());
  if (blendMode < 0 || blendMode >= 30) {
    return std::nullopt;
  }
  // The precompiled BlendMerge shader only supports simple TextureEffect children (no subset,
  // no DeviceSpaceTextureEffect). Reject if any child has unsupported uniforms.
  for (size_t i = 0; i < xfp->numChildProcessors(); i++) {
    auto child = xfp->childProcessor(i);
    if (child == nullptr) {
      continue;
    }
    if (child->name() == "DeviceSpaceTextureEffect") {
      return std::nullopt;
    }
    if (child->name() == "TextureEffect") {
      auto* childTE = static_cast<const TextureEffect*>(child);
      if (childTE->numTextureSamplers() == 0 || childTE->hasSubset()) {
        return std::nullopt;
      }
    }
  }
  using FD = BlendMergeShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::BLEND_MODE] = blendMode;
  fragValues[FD::CHILD_TYPE] = childType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"BlendMergeShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchHairlineLine(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "HairlineLineGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* hlgp = static_cast<const HairlineLineGeometryProcessor*>(gp);
  using D = HairlineLineShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_AA] = hlgp->getAAType() == AAType::Coverage ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"HairlineLineShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchHairlineQuad(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "HairlineQuadGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* hqgp = static_cast<const HairlineQuadGeometryProcessor*>(gp);
  using D = HairlineQuadShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_AA] = hqgp->getAAType() == AAType::Coverage ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"HairlineQuadShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchEllipseFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "EllipseGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* egp = static_cast<const EllipseGeometryProcessor*>(gp);
  using D = EllipseFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::STROKE] = egp->isStroke() ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = egp->hasCommonColor() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"EllipseFillShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchComplexEllipseFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ComplexEllipseGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* cegp = static_cast<const ComplexEllipseGeometryProcessor*>(gp);
  using D = ComplexEllipseFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::STROKE] = cegp->isStroke() ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = cegp->hasCommonColor() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"ComplexEllipseFillShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchNonAARRectFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "NonAARRectGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* ngp = static_cast<const NonAARRectGeometryProcessor*>(gp);
  using D = NonAARRectFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COMMON_COLOR] = ngp->hasCommonColor() ? 1 : 0;
  values[D::STROKE] = ngp->isStroke() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"NonAARRectFillShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchComplexNonAARRectFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ComplexNonAARRectGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* cngp = static_cast<const ComplexNonAARRectGeometryProcessor*>(gp);
  using D = ComplexNonAARRectFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COMMON_COLOR] = cngp->hasCommonColor() ? 1 : 0;
  values[D::STROKE] = cngp->isStroke() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"ComplexNonAARRectFillShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchRoundStrokeRectFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "RoundStrokeRectGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* rsgp = static_cast<const RoundStrokeRectGeometryProcessor*>(gp);
  // Skip perspective UV transforms — not covered by precompiled variants.
  if (rsgp->isUVPerspective()) {
    return std::nullopt;
  }
  using D = RoundStrokeRectFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_AA] = rsgp->getAAType() == AAType::Coverage ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = rsgp->hasCommonColor() ? 1 : 0;
  values[D::HAS_UV_MATRIX] = rsgp->hasUVMatrixTransform() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"RoundStrokeRectFillShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchShapeInstancedFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ShapeInstancedGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* sigp = static_cast<const ShapeInstancedGeometryProcessor*>(gp);
  using D = ShapeInstancedFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COLORS] = sigp->getHasColors() ? 1 : 0;
  values[D::HAS_AA] = sigp->getAAType() == AAType::Coverage ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"ShapeInstancedFillShader", index, index};
}

static std::optional<PermutationMatchResult> TryMatchMeshFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "MeshGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->getXferProcessor() != EmptyXferProcessor::GetInstance()) {
    return std::nullopt;
  }
  auto* mgp = static_cast<const MeshGeometryProcessor*>(gp);
  using D = MeshFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_TEX_COORDS] = mgp->getHasTexCoords() ? 1 : 0;
  values[D::HAS_COLORS] = mgp->getHasColors() ? 1 : 0;
  values[D::HAS_COVERAGE] = mgp->getHasCoverage() ? 1 : 0;
  auto index = domain.encode(values);
  return PermutationMatchResult{"MeshFillShader", index, index};
}

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo) {
  // Precompiled shaders assume RGBA output. Non-RGBA render targets (e.g. ALPHA_8) require an
  // output swizzle that the precompiled shader does not include. Fall back to ProgramBuilder.
  if (programInfo->getOutputSwizzle() != Swizzle::RGBA()) {
    return std::nullopt;
  }
  if (auto result = TryMatchTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchQuadColorFill(programInfo)) {
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
  if (auto result = TryMatchSingleIntervalGradient(programInfo)) {
    return result;
  }
  if (auto result = TryMatchDualIntervalGradient(programInfo)) {
    return result;
  }
  if (auto result = TryMatchTextureGradient(programInfo)) {
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
  if (auto result = TryMatchAlphaThreshold(programInfo)) {
    return result;
  }
  if (auto result = TryMatchLuma(programInfo)) {
    return result;
  }
  if (auto result = TryMatchColorSpaceXform(programInfo)) {
    return result;
  }
  if (auto result = TryMatchGaussianBlur1D(programInfo)) {
    return result;
  }
  if (auto result = TryMatchBlendMerge(programInfo)) {
    return result;
  }
  if (auto result = TryMatchHairlineLine(programInfo)) {
    return result;
  }
  if (auto result = TryMatchHairlineQuad(programInfo)) {
    return result;
  }
  if (auto result = TryMatchEllipseFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchComplexEllipseFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchNonAARRectFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchComplexNonAARRectFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchRoundStrokeRectFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchShapeInstancedFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchMeshFill(programInfo)) {
    return result;
  }
  return std::nullopt;
}

}  // namespace tgfx
