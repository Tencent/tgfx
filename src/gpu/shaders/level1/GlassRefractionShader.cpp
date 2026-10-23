/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//       https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/level1/GlassRefractionShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/shaders/PermutationRules.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "layers/processors/GlassShapeGeometryFragmentProcessor.h"

namespace tgfx {

std::optional<PermutationMatchResult> GlassRefractionShader::Match(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp == nullptr) {
    return std::nullopt;
  }
  // The GP selects the vertex layout and the initial-coverage source. Both forms must be the
  // common-color attribute layout: the kernels read the Color uniform, and a per-vertex color
  // draw would quantize fractional paint colors through the UByte4 attribute.
  int gpKind = -1;
  if (gp->name() == "EllipseGeometryProcessor") {
    auto* egp = static_cast<const EllipseGeometryProcessor*>(gp);
    if (!egp->hasCommonColor()) {
      return std::nullopt;
    }
    gpKind = 0;
  } else if (gp->name() == "QuadPerEdgeAAGeometryProcessor") {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    // The uvMatrix form is required: it declares no uvCoord attribute, so the GP transforms the
    // position for the background coordinate, which is what the quad vertex kernel reads. A
    // subset-carrying quad has a different transform contract and stays on the fallback route.
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset() ||
        quadGP->getHasUVPerspective()) {
      return std::nullopt;
    }
    gpKind = 1;
  } else {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  // The only coverage processor the kernel takes is the quad form's local shape mask (a SrcIn
  // ShaderMaskFilter over a hardware-tiled RGBA texture), applied through the HasLocalMask runtime
  // branch. It needs an affine mask transform, like the background coordinate below.
  if (programInfo->numFragmentProcessors() != 1) {
    auto coverage = ClassifyCoverageFP(programInfo);
    if (gpKind != 1 || programInfo->numFragmentProcessors() != 2 || !coverage ||
        *coverage != CoverageKind::LocalMask) {
      return std::nullopt;
    }
    auto mask = programInfo->getFragmentProcessor(1)->childProcessor(0);
    if (mask->numCoordTransforms() != 1 || mask->coordTransform(0)->matrix.hasPerspective()) {
      return std::nullopt;
    }
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "GlassRefractionFragmentProcessor") {
    return std::nullopt;
  }
  auto* refraction = static_cast<const GlassRefractionFragmentProcessor*>(fp);
  if (refraction->numChildProcessors() != 1 || refraction->numTextureSamplers() != 1) {
    return std::nullopt;
  }
  // Both vertex kernels emit the affine coordinate form ((Matrix * vec3(pos, 1)).xy computed in
  // the vertex stage), matching GeometryProcessor::emitTransforms' non-perspective branch. A
  // projective transform makes the runtime emit a vec3 varying divided in the fragment stage, so
  // it must ride the runtime route, as every plain-coordinate kernel does with projective sources.
  if (fp->numCoordTransforms() > 0 && fp->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }
  auto child = refraction->childProcessor(0);
  if (child == nullptr) {
    return std::nullopt;
  }
  // The geometry child selects the compile-time dimension because it changes the sampler layout;
  // the dispersion and lighting branches are runtime uniforms and need no dimension.
  int geometryKind = -1;
  if (child->name() == "GlassSDFGeometryFragmentProcessor") {
    auto* sdf = static_cast<const GlassSDFGeometryFragmentProcessor*>(child);
    geometryKind = sdf->getShapeType() == GlassShapeType::RoundedRect ? 0 : 1;
  } else if (child->name() == "GlassUDFGeometryFragmentProcessor") {
    // The UDF kind always declares the edge-light mask sampler; an edge-lit-off draw binds the
    // shared dummy texture there and clears the EdgeMaskOn runtime uniform.
    geometryKind = 2;
  }
  if (geometryKind < 0) {
    return std::nullopt;
  }
  GlassRefractionInputs inputs;
  inputs.gpKind = gpKind;
  inputs.geometryKind = geometryKind;
  // Same predicate the runtime folds into the program key, so the compile-time variant tracks the
  // runtime program one-for-one.
  inputs.dispersionOn = refraction->hasDispersion();
  inputs.xpType = xpType;
  auto composed = ComposeGlassRefraction(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = GlassRefractionShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = GlassRefractionShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"GlassRefractionShader", vertIndex, fragIndex};
}

}  // namespace tgfx
