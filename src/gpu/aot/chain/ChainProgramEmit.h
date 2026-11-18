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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "gpu/shaders/Family.h"
#include "gpu/shaders/contract/ChainComposition.h"

namespace tgfx {

class PrecompiledShaderCache;

/**
 * Where one declared uniform member's value comes from at upload time. The slot names the chain
 * instruction a per-slot parameter belongs to; the index names the array element, the leaf or the
 * coordinate transform the member addresses.
 */
enum class ChainBindingSource : uint8_t {
  // The per-slot typed parameters of the pointwise operators.
  SlotColorMatrixLinear,
  SlotColorMatrixTranslate,
  SlotLumaCoefficients,
  SlotAlphaThreshold,
  SlotConstColor,
  SlotAARect,
  SlotLocalRect,
  SlotLocalRectMatrix,
  SlotRRect,
  SlotRRectRadiiX,
  SlotRRectRadiiY,
  SlotRRectAntiAlias,
  SlotRRectMatrix,
  // The geometry-facing switches.
  GeometryColor,
  GeometryHasCommonColor,
  GeometryStroke,
  GeometryAA,
  GeometryCoverage,
  // The output formula.
  OutputSwizzle,
  XpBlendMode,
  DstTextureUpperLeft,
  DstTextureCoordScale,
  // The stored vertex stage's block.
  VertexMatrix,
  VertexTexSubsetMatrix,
  RTAdjust,
  CoordTransform,
  // The gradient source's named block.
  GradientLayoutType,
  GradientColorizerKind,
  GradientLutLeaf,
  GradientLeftBorder,
  GradientRightBorder,
  GradientStart,
  GradientEnd,
  GradientScale01,
  GradientBias01,
  GradientScale23,
  GradientBias23,
  GradientThreshold,
  GradientBias,
  GradientScale,
  GradientThresholds1_7,
  GradientThresholds9_13,
  GradientScales,
  GradientBiases,
  // The noise source.
  NoiseFrequencyStitch,
  NoiseModeOctaves,
  // The color-space transform's block.
  CSFlags,
  CSTransferSrc0,
  CSTransferSrc1,
  CSTransferSrcType,
  CSOotfSrc,
  CSColorXform,
  CSOotfDst,
  CSTransferDst0,
  CSTransferDst1,
  CSTransferDstType,
  // The sampler children.
  LeafSubset,
  LeafAlphaStart,
  LeafHasRgbaaa,
  LeafSubsetClampOn,
  LeafReadSwizzles,
  DeviceCoordMatrix,
  HasMaskTexture,
  DeviceMaskSubset,
  DeviceLeafMask,
  DeviceLeafCoordMatrix,
};

/// One declared uniform member: its std140 placement and the reader the upload walks.
struct ChainBinding {
  ChainBindingSource source = ChainBindingSource::OutputSwizzle;
  int slot = -1;
  int index = -1;
  uint32_t offset = 0;
  uint32_t size = 0;
};

/// Which sampler-binding child one declared sampler reads.
enum class ChainSamplerSource : uint8_t {
  Leaf,
  Lut,
  NoiseLookup,
  Mask,
  DstTexture,
};

struct ChainSamplerBinding {
  std::string name = {};
  ChainSamplerSource source = ChainSamplerSource::Leaf;
  int index = 0;
};

/**
 * The emit's own description of an assembled chain program: the members the emitted fragment
 * declares (each with the reader that fills it), the stored vertex stage's members, the samplers
 * in declaration order, and the varying contract both stages carry. The upload walks these tables,
 * so the declarations and the writer cannot disagree: nothing exists that is not read.
 */
struct ChainBindings {
  std::vector<ChainBinding> vertex = {};
  std::vector<ChainBinding> fragment = {};
  std::vector<ChainSamplerBinding> samplers = {};
  /// The leaf coordinate varyings the composition reads, and whether the resource coordinates
  /// (gradient/noise) are read.
  std::set<int> leafVaryings = {};
  bool needsSourceCoords = false;
  /// The coordinate transform index offset of leaf 0: a resource profile gives transform 0 to the
  /// gradient/noise source, so its leaves start at 1. The stored vertex stage's declares and the
  /// interpreter's own hook follow the same shift.
  uint8_t leafTransformShift = 0;
  uint32_t fragmentBlockSize = 0;
};

/**
 * Emits a complete chain fragment the way the JIT's ProgramBuilder did: one walk generates every
 * declaration it needs — the uniform block, the varyings and the samplers — and the code is the
 * per-op pack entries the composition actually reaches. A member exists only because the emission
 * that reads it registered it, so the declaration is the emission's closure: no parameter pool, no
 * packed slot array, no capacity-sized arrays, no prefix the writer has to know about.
 * @param composition  The shape to emit.
 * @param family       The matched family (the variant walk's input).
 * @param variant      The matched variant (its geometry, texture count and xfer fact).
 * @param cache        The loaded bundle's pack.
 * @param code         Receives the fragment text.
 * @param bindings     Receives the declaration tables the upload walks.
 * @return             False when the shape is outside the slice or a pack entry is missing; the
 *                     caller keeps its other paths.
 */
bool EmitChainProgramGLSL(const ChainComposition& composition, const Family& family,
                          const Variant& variant, const PrecompiledShaderCache* cache,
                          std::string* code, ChainBindings* bindings);

/**
 * Assembles the vertex stage from the stored variant text and the emit's varying contract: the
 * attributes, the vertex block and the geometry hook stay byte-identical (the writer's addresses),
 * while the varying set follows the composition.
 * @param bindings   The emit's contract: the leaf coordinate varyings and the resource reads.
 * @param storedVs   The variant's stored vertex text.
 * @param code       Receives the vertex text.
 * @return           False when the stored text carries no chain vertex hook.
 */
bool AssembleChainVertexGLSL(const ChainBindings& bindings, const std::string& storedVs,
                             std::string* code);

}  // namespace tgfx
