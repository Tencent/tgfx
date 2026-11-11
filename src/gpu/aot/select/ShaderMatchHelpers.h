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
#include "gpu/aot/ir/XferFacts.h"
#include "gpu/shaders/contract/AOTFailure.h"
#include "gpu/shaders/contract/DrawFacts.h"

// Shared predicates of the per-family Match functions. Declared here and defined once in
// PermutationMatcher.cpp (their private helper chains stay internal to that translation unit);
// each family's .cpp includes this header, so family matching code never duplicates them.
namespace tgfx {

class ProgramInfo;
class GeometryProcessor;
class FragmentProcessor;
class XferProcessor;
struct DrawIR;
struct ColorSpaceXformSteps;
enum class AOTOpaqueKernel : int;
struct AOTOpaqueKernelParameters;
struct AOTTextureParameters;
class QuadPerEdgeAAGeometryProcessor;
struct AOTProgram;
struct AOTCoverageTerm;
struct Family;

/// Describes a geometry processor's matcher-relevant facts — the one description of the draw's
/// geometry the executor-table matching reads, taken off the made processor at match time. The
/// type dispatch uses IsProcessor, never name() strings, so a rename cannot silently reclassify.
AOTGeometryDesc DescribeGeometryProcessor(const GeometryProcessor* gp);

/// The pointwise chain's gpKind axis: one value per vertex layout its kernels have. The numbers are
/// permutation values baked into the kernels' GP layouts, so they are ABI — append only.
namespace GpKind {
inline constexpr int Default = 0;
inline constexpr int QuadPerEdgeAA = 1;
inline constexpr int Mesh = 2;
inline constexpr int AtlasText = 3;
inline constexpr int Ellipse = 4;
inline constexpr int HairlineLine = 5;
inline constexpr int ComplexEllipseFill = 6;
inline constexpr int ComplexEllipseStroke = 7;
}  // namespace GpKind

/// Maps the one geometry classification onto the chain's gpKind axis; nullopt when the chain has no
/// vertex layout for that geometry. The single place the two classifications meet.
std::optional<int> PointwiseGpKind(const AOTGeometryDesc& geometry);

/// The xfer classification the matcher facts read — what GetXPType reports for the draw's info,
/// taken off its output-formula facts.
XferKind ClassifyXferForMatch(const XferFacts& xfer);

/// Whether a color-space transform's steps fit the single-slot fill kernel's precompiled
/// evaluation (the transfer functions must be of an indexable type when linearize/encode run) —
/// IsSupportedPointwiseOp's color-space half, over the IR's steps.
bool SupportedFillKernelSteps(const ColorSpaceXformSteps* steps);

/// 0 = Empty XferProcessor, 1 = PorterDuff DST_TEX, 2 = PorterDuff FBF, -1 = none representable.
int GetXPType(const ProgramInfo* programInfo);

/// Whether a ShaderMode value is one the tiled fill kernel implements.
bool TiledFillModeSupported(int mode);

/// The opaque-kernel families' IR contract (the barrier node, read by the family's matching): the
/// named kernel's node facts, and — for the blur kernels — the servable texture leaf it reads.
/// The families walk no processor tree; the node IS the contract.
struct OpaqueKernelIRShape {
  const AOTOpaqueKernelParameters* kernel = nullptr;
  const AOTTextureParameters* childTexture = nullptr;
};
std::optional<OpaqueKernelIRShape> ReadOpaqueKernelIRShape(const DrawIR* ir, AOTOpaqueKernel kernel,
                                                           bool requiresTextureChild);

/// Records why a candidate rejected the draw into the match's failure out-parameter (the first
/// rejection wins) and returns nullopt.
std::nullopt_t Fail(PermutationMatchFailure* failure, PermutationMatchFailure reason);

/// Classifies one lowered coverage term (see Coverage); nullopt when it cannot be classified.
std::optional<Coverage> ClassifyCoverageTerm(const AOTCoverageTerm& term);

/// The bare texture leaf of a non-analytic coverage term: the run lowered a single TextureEffect
/// over the geometry-coverage unit, so the graph is exactly the unit plus the texture source.
const AOTTextureParameters* BareTextureCoverageLeaf(const AOTCoverageTerm& term);

/// Classifies the coverage stage of a ProgramInfo carrying at most one coverage FP: the
/// no-coverage case is a count, the rest reads the carried IR's terms (the atlas glyph term is
/// skipped — the geometry processor's own coverage, never a coverage FP).
std::optional<Coverage> ClassifyCoverageTerms(const ProgramInfo* programInfo);

/// The local-mask texture leaf behind a LocalMask classification (the SrcIn ShaderMaskFilter
/// fingerprint): the leaf carries the mask transform's perspective fact, so a family that needs an
/// affine mask reads it off the IR instead of the coverage processor's child transform. Null when
/// the draw has no such term.
const AOTTextureParameters* LocalMaskCoverageLeaf(const DrawIR* ir);

/// The device-space mask leaf behind a DeviceMask classification: the term's own sampler facts
/// (alpha-only, perspective), so a family that needs an affine alpha mask reads them off the IR
/// instead of the mask processor. Null when the draw has no such term.
const AOTTextureParameters* DeviceMaskCoverageLeaf(const DrawIR* ir);

/// Whether a ShaderMode value is one the blur kernels' shared tiled sampling implements.
bool TiledModeSupported(int mode);

/// Whether a subset-carrying quad's subset is servable by the chain kernel's runtime uniforms; the
/// decision reads the compiled chain program's binding table and tiled/device leaf records.
bool IsChainQuadSubsetServable(const QuadPerEdgeAAGeometryProcessor* quadGP,
                               const AOTProgram& program);

/// Each family's Match opens with this guard: a kernel that does not apply the write swizzle at
/// output (ServesAlphaOnly == false) refuses draws onto alpha-only (ALPHA_8) targets, so the
/// predicate and the family's declared capability cannot drift apart. The dispatch's alpha-only
/// gate and the debug audit both rely on this agreement.
bool RejectsAlphaOnlyTarget(bool servesAlphaOnly, const ProgramInfo* programInfo);

/// The acceptance gates every route into a precompiled program must pass, stated once: the output
/// swizzles the kernels implement, the OpenGL profile/sampler gate, and — when family names
/// the matched family — the texture-read semantics that family declares (default deny, so a null
/// family skips that check and it runs once the family is known). A route that skips one of
/// these gates would serve a draw the kernels cannot render correctly.
bool AdmitMatchedProgram(const ProgramInfo* programInfo, const DrawShape& shape,
                         const Family* family, PermutationMatchFailure* failure);

}  // namespace tgfx
