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

#include "gpu/shaders/kernels/QuadTextureFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"
#include "gpu/shaders/modules/TextureSourceModule.h"

namespace tgfx {

std::optional<QuadTextureFillShader::Inputs> QuadTextureFillShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<QuadPerEdgeAAGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  // Color must be exactly one TextureEffect. Coverage is optional: a device-space mask maps to
  // HAS_MASK_TEXTURE, while a local-space texture-alpha mask maps to HAS_LOCAL_MASK.
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  bool hasLocalMask = false;
  if (programInfo->numFragmentProcessors() != 1) {
    if (HasDirectAnalyticClipCoverage(programInfo)) {
      // Evaluated through the runtime clip contract uniforms.
    } else {
      auto coverage = ClassifyCoverageFP(programInfo);
      if (coverage && *coverage == CoverageKind::DeviceMask) {
        // Device masks are a runtime uniform now, so there is nothing to encode.
      } else if (coverage && *coverage == CoverageKind::LocalMask) {
        hasLocalMask = true;
      } else {
        // Unrecognized coverage kinds (including unclassifiable ones) stay on other routes.
        return std::nullopt;
      }
    }
  }
  int xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (!IsProcessor<TextureEffect>(fp)) {
    return std::nullopt;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  auto* te = static_cast<const TextureEffect*>(fp);
  if (te->numTextureSamplers() == 0) {
    return std::nullopt;
  }
  if (te->isYUV()) {
    return std::nullopt;
  }
  // No color-texture guards are needed for the local mask: the coverage texture writes its
  // per-texture uniforms (Subset/AlphaOnly/HasRgbaaa) under a distinct structural ordinal
  // (Subset_1, ...), so it can no longer clobber the color texture's Subset / AlphaOnly slots. The
  // color may therefore carry a subset, be alpha-only, or use RGBAAA freely.
  // Subset clamping logic:
  //   gpSubset=true  -> HAS_SUBSET=1: vertex shader outputs vTexSubset varying, fragment double-
  //                     clamps by the varying and the Subset uniform.
  //   gpSubset=false -> HAS_SUBSET=0: fragment always clamps by the Subset uniform alone. The
  //                     uniform is populated from computeSubsetRect, which yields the full texture
  //                     bounds when the source has no real subset, so the clamp is a no-op. This
  //                     subsumes the former HAS_CLAMP_SUBSET dimension.
  Inputs inputs;
  inputs.hasUVCoord = !quadGP->hasUVMatrix();
  inputs.hasSubset = quadGP->getHasSubset();
  // The precompiled vTexSubset path transforms the subset corners WITHOUT a perspective divide
  // (quad_texture_fill.vert: perspLT = (CoordTransformMatrix_0 * vec3(src, 1)).xy), while the
  // runtime route's emitTransformedPoint divides by w when the transform is projective. A
  // projective source carrying a GP subset would clamp the texture coordinate to wrong bounds
  // (one edge-line class of A/B divergence), so it must stay on the runtime route — the same
  // conservative policy the plain-coordinate kernels apply to projective sources. Projective
  // draws without a GP subset are fine: TransformedCoords_0 is a vec3 varying that the fragment
  // shader divides.
  if (inputs.hasSubset && (quadGP->getHasUVPerspective() || te->hasPerspective())) {
    return std::nullopt;
  }
  inputs.hasLocalMask = hasLocalMask;
  inputs.xpType = xpType;
  return inputs;
}

ModuleBindFn QuadTextureFillShader::BindingFor(const ProgramInfo* programInfo,
                                               const FragmentProcessor* fp) {
  // The analytic clip (coverage section) feeds the clip module.
  if (IsProcessor<RectEffect>(fp)) {
    return &AnalyticClipModule::Bind;
  }
  // Only the color root, and only non-YUV sources (the YUV fields ride the processor's own
  // writer until their module exists); every other position keeps onSetData.
  if (fp != programInfo->getFragmentProcessor(0) || !IsProcessor<TextureEffect>(fp)) {
    return nullptr;
  }
  if (static_cast<const TextureEffect*>(fp)->isYUV()) {
    return nullptr;
  }
  return &TextureSourceModule::Bind;
}

XPBindFn QuadTextureFillShader::XPBindingFor(const ProgramInfo* programInfo) {
  auto* xp = programInfo->getXferProcessor();
  if (!IsProcessor<PorterDuffXferProcessor>(xp)) {
    return nullptr;
  }
  return &PorterDuffXPModule::Bind;
}

}  // namespace tgfx
