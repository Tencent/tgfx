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

#pragma once

#include <optional>

// Shared predicates of the per-family Match functions. Declared here and defined once in
// PermutationMatcher.cpp (their private helper chains stay internal to that translation unit);
// each family's .cpp includes this header, so family matching code never duplicates them.
namespace tgfx {

class ProgramInfo;
class GeometryProcessor;
class FragmentProcessor;
class QuadPerEdgeAAGeometryProcessor;
class AOTPointwiseChainProcessor;
struct PermutationMatchResult;
struct PermutationVariant;
enum class PermutationMatchFailure;

/// How the draw's sole coverage FP is served. LocalMask is a distinct capability: it needs a
/// local-space varying and sampler and therefore cannot be encoded in the shared runtime clip
/// contract.
enum class CoverageKind {
  None,
  AARect,
  RRect,
  DeviceMask,
  LocalMask,
};

/// 0 = Empty XferProcessor, 1 = PorterDuff DST_TEX, 2 = PorterDuff FBF, -1 = none representable.
int GetXPType(const ProgramInfo* programInfo);

/// True when the draw carries no fragment processors at all, or exactly one direct analytic clip
/// coverage FP (AA rect or rrect) that the runtime clip contract uniforms can evaluate.
bool HasNoProcessorsOrDirectClip(const ProgramInfo* programInfo);

/// 0 = DefaultGeometryProcessor, 1 = QuadPerEdgeAAGeometryProcessor, -1 = anything else.
int GetGPType(const GeometryProcessor* gp);

/// True when the GP is a position-only rect form (Default, or a Quad with common color, a UV
/// matrix and no subset): the plain fill kernels' vertex contract.
bool IsPositionOnlyRectGP(const GeometryProcessor* gp);

/// 1 when the sole coverage FP is a device-space mask, 0 when there is no coverage FP at all,
/// nullopt for anything the shared runtime-clip contract cannot evaluate.
std::optional<int> SharedDeviceMaskValue(const ProgramInfo* programInfo);

/// Whether a ShaderMode value is one the tiled fill kernel implements.
bool TiledFillModeSupported(int mode);

/// The child a blur kernel samples per tap: a plain Clamp TextureEffect or a supported
/// TiledTextureEffect (see the definition for the exact restrictions).
bool IsServableBlurChild(const FragmentProcessor* childFP);

/// Whether the FP is one of the four pointwise operators (ColorMatrix / Luma / AlphaThreshold /
/// ColorSpaceXform) that share the TexturedEffect structural class.
bool IsSupportedPointwiseOp(const FragmentProcessor* fp);

/// Records why a candidate rejected the draw (a thread-local reason consumed by MatchPermutation)
/// and returns nullopt.
std::nullopt_t Fail(PermutationMatchFailure reason);

/// Classifies the coverage FP itself (see CoverageKind); nullopt when it cannot be classified.
std::optional<CoverageKind> ClassifyCoverage(const FragmentProcessor* coverageFP);

/// Classifies the coverage stage of a ProgramInfo carrying at most one coverage FP.
std::optional<CoverageKind> ClassifyCoverageFP(const ProgramInfo* programInfo);

/// Direct analytic clip coverage: the sole coverage FP is an AA rect or a leaf rrect.
bool HasDirectAnalyticClipCoverage(const ProgramInfo* programInfo);

/// Whether a ShaderMode value is one the blur kernels' shared tiled sampling implements.
bool TiledModeSupported(int mode);

/// Whether a subset-carrying quad's subset is servable by the chain kernel's runtime uniforms.
bool IsChainQuadSubsetServable(const QuadPerEdgeAAGeometryProcessor* quadGP,
                               const AOTPointwiseChainProcessor* chain);

/// Each family's Match opens with this guard: a kernel that does not apply the write swizzle at
/// output (ServesAlphaOnly == false) refuses draws onto alpha-only (ALPHA_8) targets, so the
/// predicate and the family's declared capability cannot drift apart. The dispatch's alpha-only
/// gate and the debug audit both rely on this agreement.
bool RejectsAlphaOnlyTarget(bool servesAlphaOnly, const ProgramInfo* programInfo);

/// True when any fragment processor in the tree is a gradient source (ClampedGradientEffect):
/// only the interpreter's extended resource kind serves gradients, so the decomposition switch
/// keeps serving them even when the rest of the interpreter route is switched off.
bool ContainsGradientSource(const FragmentProcessor* processor);

}  // namespace tgfx
