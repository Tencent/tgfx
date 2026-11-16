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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/write/modules/ChainWrite.h"
#include <vector>
#include "gpu/CoordTransform.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/select/FamilyMatchResult.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/shaders/modules/Chain.h"
#include "gpu/shaders/modules/ChainAtlasGeometry.h"
#include "gpu/shaders/modules/ChainColorParams.h"
#include "gpu/shaders/modules/ChainRectGeometry.h"
#include "gpu/shaders/modules/ChainRectUVGeometry.h"

namespace tgfx {
namespace {

constexpr uint8_t ChainCoordTransformBase = UniformElementIndexOf(Chain, "CoordTransformMatrix_0");
constexpr uint8_t ChainCoordTransformCount = 6;

/// Writes the geometry's coordinate transforms into the Chain module's element range: the base
/// matrix is concatenated onto each transform's own matrix, in graph order.
void WriteChainCoordTransforms(SlotWriter* chainWriter, const Matrix& baseMatrix,
                               const std::vector<CoordTransform>& transforms) {
  auto count = static_cast<uint8_t>(
      std::min(transforms.size(), static_cast<size_t>(ChainCoordTransformCount)));
  for (uint8_t index = 0; index < count; ++index) {
    Matrix combined = {};
    combined.setConcat(transforms[index].getTotalMatrix(), baseMatrix);
    chainWriter->setIfPresent(static_cast<uint8_t>(ChainCoordTransformBase + index), combined);
  }
}

/// The base matrix each geometry scheme concatenates onto the leaves' transforms: the rect layouts
/// apply their uv matrix, the atlas and the per-pixel coverage layouts sample in their own space,
/// and the hairline layouts only carry one when the draw supplied it.
void WriteChainTransformBases(SlotWriter* chainWriter, const AOTGeometryParameters& p,
                              uint8_t geometryIndex,
                              const std::vector<CoordTransform>& transforms) {
  switch (geometryIndex) {
    case 0:
      if (p.kind == GeometryKind::Mesh) {
        WriteChainCoordTransforms(chainWriter, Matrix::I(), transforms);
      } else if (p.kind == GeometryKind::Default || p.kind == GeometryKind::QuadPerEdgeAA) {
        WriteChainCoordTransforms(chainWriter, p.uvMatrix, transforms);
      }
      break;
    case 1:
      WriteChainCoordTransforms(
          chainWriter, p.kind == GeometryKind::Mesh ? Matrix::I() : p.uvMatrix, transforms);
      break;
    case 4:
    case 8:
      if (p.hasUVMatrix) {
        WriteChainCoordTransforms(chainWriter, p.uvMatrix, transforms);
      }
      break;
    default:
      WriteChainCoordTransforms(chainWriter, Matrix::I(), transforms);
      break;
  }
}

void WriteChainModule(WriteContext& context) {
  const auto* program = context.sources.chain;
  if (program == nullptr) {
    return;
  }
  const auto transforms = BuildChainCoordTransforms(*program);
  WriteChainTransformBases(context.slot, context.geometry, context.sources.variant.geometry,
                           transforms);
  // The blend switch's fact: a Blend slot's mode or an advanced shader-side xfer mode.
  const bool needsBlendMath =
      ChainNeedsBlendMath(*program, context.info.getXferFacts().shaderBlend() &&
                                        static_cast<int>(context.info.getBlendMode()) >=
                                            static_cast<int>(BlendMode::Overlay));
  BindChainProgram(*program, context.slot, needsBlendMath);
  BindChainBindings(*program, context.slot);
}

/// The interpreter's geometry-facing switches: one write per geometry scheme, from the geometry
/// parameters the layout already carries. The AA switch is the same formula the dedicated hairline
/// writers use, so both routes antialias the edge identically on an MSAA target.
void WriteChainColorParamsModule(WriteContext& context) {
  constexpr uint8_t Color = UniformElementIndexOf(ChainColorParams, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ChainColorParams, "HasCommonColor");
  constexpr uint8_t StrokeEnabled = UniformElementIndexOf(ChainColorParams, "StrokeEnabled");
  constexpr uint8_t AAEnabled = UniformElementIndexOf(ChainColorParams, "AAEnabled");
  constexpr uint8_t Coverage = UniformElementIndexOf(ChainColorParams, "Coverage");
  const auto& p = context.geometry;
  context.slot->set(Color, p.commonColor);
  context.slot->set(HasCommonColor, static_cast<int32_t>(p.hasCommonColor ? 1 : 0));
  context.slot->set(StrokeEnabled, static_cast<int32_t>(p.isStroke ? 1 : 0));
  context.slot->set(AAEnabled, static_cast<int32_t>(p.aaType == AAType::None ? 0 : 1));
  context.slot->set(Coverage, p.coverage);
}

/// The uvless rect layout: the vertex stage projects through its own view matrix, and a quad draw
/// carrying a subset exposes the first leaf's total transform as the subset matrix.
void WriteChainRectGeometryModule(WriteContext& context) {
  constexpr uint8_t Matrix = UniformElementIndexOf(ChainRectGeometry, "Matrix");
  constexpr uint8_t TexSubsetMatrix = UniformElementIndexOf(ChainRectGeometry, "InTexSubsetMatrix");
  const auto& p = context.geometry;
  switch (p.kind) {
    case GeometryKind::Default:
      context.slot->set(Matrix, p.viewMatrix);
      break;
    case GeometryKind::QuadPerEdgeAA: {
      context.slot->set(Matrix, Matrix::I());
      if (p.hasSubset && p.hasUVMatrix && context.sources.chain != nullptr) {
        const auto transforms = BuildChainCoordTransforms(*context.sources.chain);
        if (!transforms.empty()) {
          context.slot->setIfPresent(TexSubsetMatrix, transforms.front().getTotalMatrix());
        }
      }
      break;
    }
    case GeometryKind::Mesh:
      context.slot->set(Matrix, p.viewMatrix);
      break;
    default:
      break;
  }
}

/// The uv-attribute rect layout: every leaf reads either the attribute or the local coordinate,
/// selected per leaf by the runtime coord-source mask.
void WriteChainRectUVGeometryModule(WriteContext& context) {
  constexpr uint8_t Matrix = UniformElementIndexOf(ChainRectUVGeometry, "Matrix");
  constexpr uint8_t CoordSourceMask = UniformElementIndexOf(ChainRectUVGeometry, "CoordSourceMask");
  const auto& p = context.geometry;
  if (context.sources.chain != nullptr) {
    context.slot->set(CoordSourceMask, static_cast<int>(context.sources.chain->coordSourceMask));
  }
  switch (p.kind) {
    case GeometryKind::Default:
    case GeometryKind::Mesh:
      context.slot->set(Matrix, p.viewMatrix);
      break;
    default:
      context.slot->set(Matrix, Matrix::I());
      break;
  }
}

/// The glyph atlas layout: the mask coordinate rides the uv slot, so the projection is identity.
void WriteChainAtlasGeometryModule(WriteContext& context) {
  constexpr uint8_t Matrix = UniformElementIndexOf(ChainAtlasGeometry, "Matrix");
  constexpr uint8_t CoordSourceMask = UniformElementIndexOf(ChainAtlasGeometry, "CoordSourceMask");
  context.slot->set(Matrix, Matrix::I());
  if (context.sources.chain != nullptr) {
    context.slot->set(CoordSourceMask, static_cast<int>(context.sources.chain->coordSourceMask));
  }
}

}  // namespace

ModuleWriter ChainWriterOf(const Module& module) {
  if (&module == &Chain) {
    return WriteChainModule;
  }
  if (&module == &ChainColorParams) {
    return WriteChainColorParamsModule;
  }
  return nullptr;
}

ModuleWriter ChainGeometryWriterOf(const Module& module) {
  if (&module == &ChainRectGeometry) {
    return WriteChainRectGeometryModule;
  }
  if (&module == &ChainRectUVGeometry) {
    return WriteChainRectUVGeometryModule;
  }
  if (&module == &ChainAtlasGeometry) {
    return WriteChainAtlasGeometryModule;
  }
  return nullptr;
}

}  // namespace tgfx
