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

#include "ProgramWriter.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include "Program.h"
#include "ProgramInfo.h"
#include "core/utils/Log.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/DrawFacts.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/modules/Chain.h"
#include "gpu/shaders/modules/ChainColorParams.h"
#include "gpu/shaders/modules/Frame.h"
#include "gpu/shaders/modules/Xfer.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// The compiled YUV tail program, mirroring ChainProgram: kept on the draw's IR, compiled here
// once when the match never needed to (the family's matching reads IR facts only).

std::array<float, 4> GetRTAdjustArray(const RenderTarget* renderTarget) {
  std::array<float, 4> result = {};
  result[0] = 2.f / static_cast<float>(renderTarget->width());
  result[2] = 2.f / static_cast<float>(renderTarget->height());
  result[1] = -1.f;
  result[3] = -1.f;
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  // WebGPU viewport Y transform is (1 - y_ndc)/2, meaning NDC y=+1 maps to framebuffer top.
  // For TopLeft origin (pixel y=0 at top), we need pixel y=0 -> NDC y=+1, requiring Y negate.
  if (renderTarget->getContext()->backend() == Backend::WebGPU &&
      renderTarget->origin() == ImageOrigin::TopLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
}

bool ProgramWriter::write(const ProgramInfo& info, RenderPass* renderPass, Program* program) const {
  if (!canWrite(info)) {
    // The route's compiled program is unavailable: submitting would draw with unwritten (or
    // garbage) uniform fields. Tell the caller to skip the draw.
    LOGE("ProgramWriter::write() the program's uniform content is unavailable; skipping the draw");
    return false;
  }
  size_t vertexOffset = 0;
  size_t fragmentOffset = 0;
  auto uniformBuffer = info.getUniformBuffer(program, &vertexOffset, &fragmentOffset);

  auto vertexUniformData = program->getUniformData(ShaderStage::Vertex);
  auto fragmentUniformData = program->getUniformData(ShaderStage::Fragment);
  if (uniformBuffer == nullptr &&
      (vertexUniformData != nullptr || fragmentUniformData != nullptr)) {
    // The program needs a uniform block but no backing store is available (allocation or map
    // failed). Aborting the draw loses one frame's content; uploading anyway would write through
    // a null pointer. Tell the caller to skip the draw.
    LOGE("ProgramWriter::write() no uniform buffer available; skipping the draw");
    return false;
  }

  writeUniformValues(info, vertexUniformData, fragmentUniformData);

  info.bindUniformBufferAndUnloadToGPU(program, std::move(uniformBuffer), renderPass, vertexOffset,
                                       fragmentOffset);
  return writeSamplers(info, program, renderPass);
}

bool ProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

void ProgramWriter::writeUniformValues(const ProgramInfo& info, UniformData* vertexUniformData,
                                       UniformData* fragmentUniformData) const {
  // One uniform entry per route: every writer here is a precompiled one, so the values come from
  // the variant's layout or the compiled program — there is no by-name fallback and no separate
  // engine-default step left.
  writeUniforms(info, vertexUniformData, fragmentUniformData);
}

namespace {

// The Chain modules' element indices: every geometry takes its constants from its own declaration
// (a mistyped name is the link error Module.h's ElementNameNotFound promises). The six coordinate
// matrices are consecutive elements of the Chain module; the per-element `when` gates bound how
// many a profile declares.
constexpr uint8_t ChainCoordTransformBase = UniformElementIndexOf(Chain, "CoordTransformMatrix_0");
constexpr uint8_t ChainCoordTransformCount = 6;

struct ChainCoordTransformWrite {
  bool hasFirst = false;
  Matrix firstTotal = {};
};

/// Writes the geometry's coordinate transforms through the Chain module's element indices: the uv
/// matrix concatenated onto each transform's own matrix, in graph order. Returns the first
/// transform's own matrix, which the plain rect geometry also exposes as its subset matrix.
ChainCoordTransformWrite WriteChainCoordTransforms(
    SlotWriter* chainWriter, const Matrix& uvMatrix,
    FragmentProcessor::CoordTransformIter* transformIter) {
  ChainCoordTransformWrite result = {};
  int index = 0;
  while (const CoordTransform* coordTransform = transformIter->next()) {
    if (index == 0) {
      result.hasFirst = true;
      result.firstTotal = coordTransform->getTotalMatrix();
    }
    if (index < ChainCoordTransformCount) {
      Matrix combined = {};
      combined.setConcat(coordTransform->getTotalMatrix(), uvMatrix);
      chainWriter->setIfPresent(static_cast<uint8_t>(ChainCoordTransformBase + index), combined);
    }
    ++index;
  }
  return result;
}

/// The plain quad layout (geometry 0): the uv matrix rides the per-quad attribute, so the vertex
/// stage keeps only its own projection. The default, quad-per-edge-AA and mesh processors share
/// this module; their writes differ in the matrix source and the common-color flag's meaning.
void WriteChainRectGeometry(SlotWriter* writer, SlotWriter* chainWriter,
                            const AOTGeometryParameters& p,
                            FragmentProcessor::CoordTransformIter* transformIter) {
  constexpr uint8_t Matrix = UniformElementIndexOf(ChainRectGeometry, "Matrix");
  constexpr uint8_t TexSubsetMatrix = UniformElementIndexOf(ChainRectGeometry, "InTexSubsetMatrix");
  constexpr uint8_t Color = UniformElementIndexOf(ChainRectGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ChainRectGeometry, "HasCommonColor");
  switch (p.kind) {
    case GeometryKind::Default: {
      WriteChainCoordTransforms(chainWriter, p.uvMatrix, transformIter);
      writer->set(Color, p.commonColor);
      writer->set(HasCommonColor, 1);
      writer->set(Matrix, p.viewMatrix);
      break;
    }
    case GeometryKind::QuadPerEdgeAA: {
      auto write = WriteChainCoordTransforms(chainWriter, p.uvMatrix, transformIter);
      writer->set(Matrix, Matrix::I());
      writer->set(HasCommonColor, p.hasCommonColor ? 1 : 0);
      if (p.hasCommonColor) {
        writer->set(Color, p.commonColor);
      }
      if (p.hasSubset && p.hasUVMatrix && write.hasFirst) {
        writer->setIfPresent(TexSubsetMatrix, write.firstTotal);
      }
      break;
    }
    case GeometryKind::Mesh: {
      writer->set(Matrix, p.viewMatrix);
      WriteChainCoordTransforms(chainWriter, Matrix::I(), transformIter);
      writer->set(Color, p.commonColor);
      writer->set(HasCommonColor, p.hasColors ? 0 : 1);
      break;
    }
    default:
      break;
  }
}

/// The uv-attribute quad layout (geometry 1): every leaf reads either the attribute or the local
/// coordinate, selected per leaf by the runtime coord-source mask.
void WriteChainRectUVGeometry(SlotWriter* writer, SlotWriter* chainWriter,
                              const AOTGeometryParameters& p, uint32_t coordSourceMask,
                              FragmentProcessor::CoordTransformIter* transformIter) {
  constexpr uint8_t Matrix = UniformElementIndexOf(ChainRectUVGeometry, "Matrix");
  constexpr uint8_t CoordSourceMask = UniformElementIndexOf(ChainRectUVGeometry, "CoordSourceMask");
  constexpr uint8_t Color = UniformElementIndexOf(ChainRectUVGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ChainRectUVGeometry, "HasCommonColor");
  writer->set(CoordSourceMask, static_cast<int>(coordSourceMask));
  switch (p.kind) {
    case GeometryKind::Default: {
      WriteChainCoordTransforms(chainWriter, p.uvMatrix, transformIter);
      writer->set(Color, p.commonColor);
      writer->set(HasCommonColor, 1);
      writer->set(Matrix, p.viewMatrix);
      break;
    }
    case GeometryKind::Mesh: {
      writer->set(Matrix, p.viewMatrix);
      WriteChainCoordTransforms(chainWriter, Matrix::I(), transformIter);
      writer->set(Color, p.commonColor);
      writer->set(HasCommonColor, p.hasColors ? 0 : 1);
      break;
    }
    default: {
      WriteChainCoordTransforms(chainWriter, p.uvMatrix, transformIter);
      writer->set(Matrix, Matrix::I());
      writer->set(HasCommonColor, p.hasCommonColor ? 1 : 0);
      if (p.hasCommonColor) {
        writer->set(Color, p.commonColor);
      }
      break;
    }
  }
}

/// The glyph atlas layout (geometry 2): the mask coordinate rides the uv slot, no coverage
/// attribute, and the vertex stage keeps identity plus the coord-source mask.
void WriteChainAtlasGeometry(SlotWriter* writer, SlotWriter* chainWriter,
                             const AOTGeometryParameters& p, uint32_t coordSourceMask,
                             FragmentProcessor::CoordTransformIter* transformIter) {
  constexpr uint8_t Matrix = UniformElementIndexOf(ChainAtlasGeometry, "Matrix");
  constexpr uint8_t CoordSourceMask = UniformElementIndexOf(ChainAtlasGeometry, "CoordSourceMask");
  constexpr uint8_t Color = UniformElementIndexOf(ChainAtlasGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ChainAtlasGeometry, "HasCommonColor");
  writer->set(Matrix, Matrix::I());
  writer->set(CoordSourceMask, static_cast<int>(coordSourceMask));
  WriteChainCoordTransforms(chainWriter, Matrix::I(), transformIter);
  if (p.hasCommonColor) {
    writer->set(Color, p.commonColor);
  }
  writer->set(HasCommonColor, p.hasCommonColor ? 1 : 0);
}

/// The per-pixel coverage rounded geometries (the ellipse, the complex ellipse and the two non-AA
/// rounded rects): the paint and the runtime stroke switch are all they carry.
/// The ellipse and rounded-rect classes carry no geometry-specific uniforms beyond the coordinate
/// transforms: their color switches live in ChainColorParams, one write for every geometry.
void WriteChainStrokeGeometry(SlotWriter* /*writer*/, SlotWriter* chainWriter,
                              SlotWriter* colorParams, const AOTGeometryParameters& p,
                              FragmentProcessor::CoordTransformIter* transformIter) {
  constexpr uint8_t Color = UniformElementIndexOf(ChainColorParams, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ChainColorParams, "HasCommonColor");
  constexpr uint8_t StrokeEnabled = UniformElementIndexOf(ChainColorParams, "StrokeEnabled");
  WriteChainCoordTransforms(chainWriter, Matrix::I(), transformIter);
  if (p.hasCommonColor) {
    colorParams->set(Color, p.commonColor);
  }
  colorParams->set(HasCommonColor, p.hasCommonColor ? 1 : 0);
  colorParams->set(StrokeEnabled, p.isStroke ? 1 : 0);
}

/// The per-pixel edge-AA hairline line and quad: a constant coverage factor plus the AA switch,
/// with the vertex stage projecting through the view matrix.
template <const Module& G>
template <const Module& G>
void WriteChainHairlineGeometry(SlotWriter* writer, SlotWriter* chainWriter,
                                SlotWriter* colorParams, const AOTGeometryParameters& p,
                                FragmentProcessor::CoordTransformIter* transformIter) {
  // The view matrix is the only geometry-owned field; the color, coverage and AA switches live in
  // the chain's own parameter module.
  constexpr uint8_t Matrix = UniformElementIndexOf(G, "Matrix");
  constexpr uint8_t Color = UniformElementIndexOf(ChainColorParams, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ChainColorParams, "HasCommonColor");
  constexpr uint8_t Coverage = UniformElementIndexOf(ChainColorParams, "Coverage");
  constexpr uint8_t AAEnabled = UniformElementIndexOf(ChainColorParams, "AAEnabled");
  if (p.hasUVMatrix) {
    WriteChainCoordTransforms(chainWriter, p.uvMatrix, transformIter);
  }
  colorParams->set(HasCommonColor, 1);
  colorParams->set(Color, p.commonColor);
  colorParams->set(Coverage, p.coverage);
  colorParams->set(AAEnabled, p.aaType == AAType::Coverage ? 1 : 0);
  writer->set(Matrix, p.viewMatrix);
}

/// The chain family's whole geometry upload: the geometry index selects the module whose
/// declaration the values are written into, and the processor kind selects the values (the same
/// rect module serves the default, quad-per-edge-AA and mesh processors).
void WriteChainGeometryUniforms(SlotWriter* geometryWriter, SlotWriter* chainWriter,
                                SlotWriter* colorParams, const AOTGeometryParameters& p,
                                uint8_t geometryIndex, uint32_t coordSourceMask,
                                FragmentProcessor::CoordTransformIter* transformIter) {
  switch (geometryIndex) {
    case 0:
      WriteChainRectGeometry(geometryWriter, chainWriter, p, transformIter);
      break;
    case 1:
      WriteChainRectUVGeometry(geometryWriter, chainWriter, p, coordSourceMask, transformIter);
      break;
    case 2:
      WriteChainAtlasGeometry(geometryWriter, chainWriter, p, coordSourceMask, transformIter);
      break;
    case 3:
      WriteChainStrokeGeometry(geometryWriter, chainWriter, colorParams, p, transformIter);
      break;
    case 4:
      WriteChainHairlineGeometry<ChainHairlineGeometry>(geometryWriter, chainWriter, p,
                                                        transformIter);
      break;
    case 5:
      WriteChainStrokeGeometry(geometryWriter, chainWriter, colorParams, p, transformIter);
      break;
    case 6:
      WriteChainStrokeGeometry(geometryWriter, chainWriter, colorParams, p, transformIter);
      break;
    case 7:
      WriteChainStrokeGeometry<ChainComplexRRectGeometry>(geometryWriter, chainWriter, p,
                                                          transformIter);
      break;
    case 8:
      WriteChainHairlineGeometry<ChainHairlineQuadGeometry>(geometryWriter, chainWriter,
                                                            colorParams, p, transformIter);
      break;
    default:
      break;
  }
}

}  // namespace

// The interpreter program the draw's match compiled. The match carries it on its result (the
// Chain family compiles to derive its variant), so the writers read the one decision instead of
// resolving — and potentially re-memoizing — the same program again.
static const AOTProgram* ChainProgramOf(const ProgramInfo& info) {
  const auto* sources = info.getSources();
  return sources != nullptr ? sources->chain : nullptr;
}

ChainProgramWriter::ChainProgramWriter(Variant variant)
    : variant(std::move(variant)), layout(DeriveLayout(this->variant)) {
  // The Chain and Xfer modules' slots in the variant's module sequence (after Frame and the
  // geometry): the layout's elementField table is indexed by those slots, so the writers address
  // the two modules' fields by element index instead of by name.
  const auto modules = ModulesOf(Families()[this->variant.family], this->variant);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    if (modules.list[slot] == &Chain) {
      chainSlot = slot;
    } else if (modules.list[slot] == &ChainColorParams) {
      colorParamsSlot = slot;
    } else if (modules.list[slot] == &Xfer) {
      xferSlot = slot;
    }
  }
}

bool ChainProgramWriter::canWrite(const ProgramInfo& info) const {
  // The chain kernel's whole uniform content, transforms and sampler order come from the compiled
  // program; without it there is nothing correct to upload.
  return ChainProgramOf(info) != nullptr;
}

void ChainProgramWriter::writeUniforms(const ProgramInfo& info, UniformData* vertexUniformData,
                                       UniformData* fragmentUniformData) const {
  ParamBuffer buffer(vertexUniformData, fragmentUniformData);
  // The engine default first: the render-target write swizzle, declared by the Xfer module in every
  // variant. Only the alpha-only (AAAA) case is non-identity; RGBA targets leave it 0 (a no-op).
  if (fragmentUniformData != nullptr) {
    SlotWriter xferWriter(layout, Xfer, xferSlot, &buffer);
    constexpr uint8_t OutputAlphaSwizzle = UniformElementIndexOf(Xfer, "OutputAlphaSwizzle");
    xferWriter.set(OutputAlphaSwizzle, info.getOutputSwizzle() == Swizzle::AAAA() ? 1 : 0);
  }
  const auto* program = ChainProgramOf(info);
  if (program == nullptr) {
    LOGE(
        "ChainProgramWriter::writeUniforms() no compiled chain program; the draw renders with "
        "unwritten uniforms");
    return;
  }
  // The program's binding table describes the artifact's transforms in leaf order; the info's own
  // processor tree orders them differently, so the GP must not consume the tree's iteration.
  auto transforms = BuildChainCoordTransforms(*program);
  FragmentProcessor::CoordTransformIter coordTransformIter(&transforms);
  // The geometry upload is addressed through the variant's own modules: the geometry the variant
  // was compiled for owns the geometry fields, and Chain owns the coordinate transforms.
  const auto modules = ModulesOf(Families()[variant.family], variant);
  constexpr uint8_t FrameSlot = 0;
  constexpr uint8_t RTAdjust = UniformElementIndexOf(Frame, "RTAdjust");
  SlotWriter frameWriter(layout, Frame, FrameSlot, &buffer);
  frameWriter.set(RTAdjust, GetRTAdjustArray(info.getRenderTarget()));
  SlotWriter chainWriter(layout, Chain, chainSlot, &buffer);
  SlotWriter geometryWriter(layout, *modules.list[geometrySlot], geometrySlot, &buffer);
  // The chain's geometry-facing runtime switches: one write for every variant, from the geometry
  // parameters the layout already carries, regardless of which geometry module is in the slot.
  SlotWriter colorParamsWriter(layout, ChainColorParams, colorParamsSlot, &buffer);
  constexpr uint8_t GeomColor = UniformElementIndexOf(ChainColorParams, "Color");
  constexpr uint8_t GeomHasCommonColor = UniformElementIndexOf(ChainColorParams, "HasCommonColor");
  constexpr uint8_t GeomStrokeEnabled = UniformElementIndexOf(ChainColorParams, "StrokeEnabled");
  constexpr uint8_t GeomAAEnabled = UniformElementIndexOf(ChainColorParams, "AAEnabled");
  constexpr uint8_t GeomCoverage = UniformElementIndexOf(ChainColorParams, "Coverage");
  {
    const auto& p = info.getGeometryLayout().parameters;
    colorParamsWriter.set(GeomColor, p.commonColor);
    colorParamsWriter.set(GeomHasCommonColor, static_cast<int32_t>(p.hasCommonColor ? 1 : 0));
    colorParamsWriter.set(GeomStrokeEnabled, static_cast<int32_t>(p.isStroke ? 1 : 0));
    colorParamsWriter.set(GeomAAEnabled, static_cast<int32_t>(p.aaType == AAType::None ? 0 : 1));
    colorParamsWriter.set(GeomCoverage, p.coverage);
  }
  WriteChainGeometryUniforms(&geometryWriter, &chainWriter, &colorParamsWriter,
                             info.getGeometryLayout().parameters, variant.geometry,
                             program->coordSourceMask, &coordTransformIter);
  // The whole chain block is addressed through the derived layout: the slot pools, the shape
  // selectors and the resource blocks all resolve their element's field once per program. The
  // draw's whole uniform content comes from the compiled program — the root block plus every
  // binding's per-child records — so no processor tree is walked.
  BindChainProgram(*program, &chainWriter);
  BindChainBindings(*program, &chainWriter);
  const auto& xfer = info.getXferFacts();
  const auto* chainIR = info.getCarriedIR();
  if (chainIR != nullptr && xfer.shaderBlend()) {
    SlotWriter xferWriter(layout, Xfer, xferSlot, &buffer);
    BindPorterDuffXfer(static_cast<int>(chainIR->blendMode), *xfer.dstTexture, &xferWriter);
  }
}

bool ChainProgramWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                       RenderPass* renderPass) const {
  const auto* chainProgram = ChainProgramOf(info);
  if (chainProgram == nullptr) {
    LOGE("ChainProgramWriter::writeSamplers() no compiled chain program; skipping the bindings");
    return false;
  }
  auto* cache = info.renderTarget->getContext()->globalCache();
  auto gpu = info.renderTarget->getContext()->gpu();
  unsigned textureBinding = 0;
  // The artifact's sampler order is the program's binding order: the DAG leaves (padded to the
  // four-leaf slots), the LUT gradient, the noise lookups, then the mask slot child. Each binding
  // carries the texture and the sampling its sampler state is built from.
  for (const auto& binding : chainProgram->bindings) {
    auto textureView =
        binding.textureProxy != nullptr ? binding.textureProxy->getTextureView() : nullptr;
    if (textureView == nullptr || textureView->getTexture() == nullptr) {
      // The binding occupies its slot whether or not the upload has landed: dropping the draw
      // here would lose content the shader guards against, and leaving the slot unbound would
      // shift every binding after it. The dummy keeps the position.
      renderPass->setTexture(textureBinding++, cache->getOrCreateDummyTexture(),
                             cache->getOrCreateDummySampler());
      continue;
    }
    SamplerState state(binding.args.tileModeX, binding.args.tileModeY, binding.args.sampling);
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    renderPass->setTexture(textureBinding++, textureView->getTexture(),
                           gpu->createSampler(descriptor));
  }
  // The dst texture always binds last.
  const auto& dstProxy = info.getXferFacts().dstProxy();
  auto dstTextureView = dstProxy != nullptr ? dstProxy->getTextureView() : nullptr;
  if (dstTextureView != nullptr) {
    auto state = SamplerState();
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    renderPass->setTexture(textureBinding++, dstTextureView->getTexture(),
                           gpu->createSampler(descriptor));
  }
  if (textureBinding != program->expectedTextureCount()) {
    LOGE("ChainProgramWriter::writeSamplers(): bound %u textures but the program declares %zu",
         textureBinding, program->expectedTextureCount());
    return false;
  }
  return true;
}

}  // namespace tgfx
