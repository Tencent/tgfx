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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <cmath>
#include "gpu/CoordTransform.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/Swizzle.h"
#include "gpu/UniformData.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/select/DrawFacts.h"
#include "gpu/aot/select/FamilyMatchResult.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/write/ModuleWriter.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/aot/write/modules/LeafWrite.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/modules/AnalyticClip.h"
#include "gpu/shaders/modules/BlurSourceGeometry.h"
#include "gpu/shaders/modules/GaussianBlur.h"
#include "gpu/shaders/modules/GlassRefraction.h"
#include "gpu/shaders/modules/GlassUDFTentBlur.h"
#include "gpu/shaders/modules/Gradient.h"
#include "gpu/shaders/modules/HairlineLineGeometry.h"
#include "gpu/shaders/modules/HairlineQuadGeometry.h"
#include "gpu/shaders/modules/MeshGeometry.h"
#include "gpu/shaders/modules/ShapeInstancedGeometry.h"

namespace tgfx {

namespace {

// MeshGeometry: the view transform, the color pair and the texture coordinate chain.
void WriteMeshGeometry(const ProgramInfo& info, const AOTGeometryParameters& params,
                       SlotWriter* writer) {
  constexpr uint8_t Matrix = UniformElementIndexOf(MeshGeometry, "Matrix");
  constexpr uint8_t Color = UniformElementIndexOf(MeshGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(MeshGeometry, "HasCommonColor");
  writer->set(Matrix, params.viewMatrix);
  writer->set(Color, params.commonColor);
  writer->set(HasCommonColor, static_cast<int32_t>(params.hasCommonColor ? 1 : 0));
  // The mesh's color source is the sole texture leaf of the color graph, the transform the legacy
  // walk reported first; a processor-free mesh has no transform to write.
  if (const auto* leaf = FirstColorTextureLeaf(info.getCarriedIR())) {
    constexpr uint8_t CoordMatrix = UniformElementIndexOf(MeshGeometry, "CoordMatrix");
    writer->set(CoordMatrix, SourceCoordMatrix(*leaf, info.getGeometryLayout()));
  }
}

// ShapeInstancedGeometry: the local-coordinate recovery, the view transform and the sampling
// coordinate. The instance offset and the instance color ride the vertex attributes. The sampling
// transform is the LAST one in the tree: the color graph's transforms come first, so a draw whose
// only fragment processors are the color ones leaves this unwritten (the instanced fill family
// carries no processor at all), and a coverage texture lands on it.
void WriteShapeInstancedGeometry(const ProgramInfo& info, const AOTGeometryParameters& params,
                                 SlotWriter* writer) {
  constexpr uint8_t UVMatrix = UniformElementIndexOf(ShapeInstancedGeometry, "UVMatrix");
  constexpr uint8_t ViewMatrix = UniformElementIndexOf(ShapeInstancedGeometry, "ViewMatrix");
  writer->set(UVMatrix, params.uvMatrix);
  writer->set(ViewMatrix, params.viewMatrix);
  // The sampling transform is the coverage texture leaf's, the last transform the legacy walk
  // reported; the instanced fill family carries no processor and leaves it unwritten.
  const auto* match = info.getSources();
  if (match != nullptr && match->maskLeaf != nullptr) {
    constexpr uint8_t CoordMatrix = UniformElementIndexOf(ShapeInstancedGeometry, "CoordMatrix");
    writer->set(CoordMatrix, SourceCoordMatrix(*match->maskLeaf, info.getGeometryLayout()));
  }
}

// Gradient: the clamped single-interval gradient's whole record — the endpoints, the boundary
// colors, the layout branch and the conic layout's bias/scale pair. The values are the ones
// BindGradientNode uploads for the legacy instanced-shape kernel; the coordinate transform rides
// the vertex block.
void WriteGradient(const AOTGradientParameters& params, SlotWriter* writer) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(GradientModule, "CoordMatrix");
  constexpr uint8_t LeftBorderColor = UniformElementIndexOf(GradientModule, "LeftBorderColor");
  constexpr uint8_t RightBorderColor = UniformElementIndexOf(GradientModule, "RightBorderColor");
  constexpr uint8_t LayoutType = UniformElementIndexOf(GradientModule, "LayoutType");
  constexpr uint8_t Bias = UniformElementIndexOf(GradientModule, "Bias");
  constexpr uint8_t Scale = UniformElementIndexOf(GradientModule, "Scale");
  constexpr uint8_t Start = UniformElementIndexOf(GradientModule, "Start");
  constexpr uint8_t End = UniformElementIndexOf(GradientModule, "End");
  writer->set(CoordMatrix, params.coordMatrix);
  writer->set(LeftBorderColor, params.leftBorder);
  writer->set(RightBorderColor, params.rightBorder);
  writer->set(LayoutType, static_cast<int32_t>(params.layoutType));
  // The conic layout is the only one the shared gradient layout maths reads these two for; the
  // other layouts record their defaults, which the shader never touches.
  writer->set(Bias, params.bias);
  writer->set(Scale, params.scale);
  writer->set(Start, params.start);
  writer->set(End, params.end);
}

void WriteBlurSourceGeometry(const ProgramInfo& info, const AOTGeometryParameters& params,
                             SlotWriter* writer) {
  constexpr uint8_t ViewMatrix = UniformElementIndexOf(BlurSourceGeometry, "Matrix");
  writer->set(ViewMatrix, params.viewMatrix);
  const auto* match = info.getSources();
  if (match != nullptr && match->blurChild != nullptr) {
    constexpr uint8_t CoordMatrix = UniformElementIndexOf(BlurSourceGeometry, "CoordMatrix");
    writer->set(CoordMatrix, SourceCoordMatrix(*match->blurChild, info.getGeometryLayout()));
  }
}

/// The child's sampling fields the blur-style bodies share: the tiled recipe or the plain subset
/// rect, plus the tiled-child selector. Both blur modules declare the same five fields, so the
/// shared body templates on the module and resolves every element index at compile time.
template <const Module& module>
void WriteChildSamplingFields(const AOTTextureParameters* child, const TextureView* view,
                              SlotWriter* writer) {
  constexpr uint8_t TiledChild = UniformElementIndexOf(module, "TiledChild");
  constexpr uint8_t ShaderModeX = UniformElementIndexOf(module, "ShaderModeX");
  constexpr uint8_t ShaderModeY = UniformElementIndexOf(module, "ShaderModeY");
  constexpr uint8_t Subset = UniformElementIndexOf(module, "Subset");
  constexpr uint8_t Clamp = UniformElementIndexOf(module, "Clamp");
  constexpr uint8_t Dimension = UniformElementIndexOf(module, "Dimension");
  const bool tiled = child != nullptr && child->tiledRecipe.has_value();
  writer->set(TiledChild, static_cast<int32_t>(tiled ? 1 : 0));
  if (tiled) {
    const auto& sampling = *child->tiledRecipe;
    writer->set(ShaderModeX, static_cast<int32_t>(sampling.shaderModeX));
    writer->set(ShaderModeY, static_cast<int32_t>(sampling.shaderModeY));
    writer->set(Subset,
                std::array<float, 4>{sampling.shaderSubset.left, sampling.shaderSubset.top,
                                     sampling.shaderSubset.right, sampling.shaderSubset.bottom});
    writer->set(Clamp,
                std::array<float, 4>{sampling.shaderClamp.left, sampling.shaderClamp.top,
                                     sampling.shaderClamp.right, sampling.shaderClamp.bottom});
    if (sampling.usesShaderDimensions) {
      writer->set(Dimension,
                  std::array<float, 2>{sampling.shaderDimensions.x, sampling.shaderDimensions.y});
    }
  } else if (child != nullptr && view != nullptr && view->getTexture() != nullptr) {
    float rect[4] = {};
    LeafSubsetRectForSampler(child->textureProxy, view, child->subset, child->samplerState, rect);
    writer->set(Subset, std::array<float, 4>{rect[0], rect[1], rect[2], rect[3]});
  }
}

void WriteGlassUDFTentBlur(const AOTOpaqueKernelParameters& params,
                           const AOTTextureParameters* child, SlotWriter* writer) {
  constexpr uint8_t Radius = UniformElementIndexOf(GlassUDFTentBlur, "Radius");
  constexpr uint8_t Field = UniformElementIndexOf(GlassUDFTentBlur, "Field");
  constexpr uint8_t InputIsPacked = UniformElementIndexOf(GlassUDFTentBlur, "InputIsPacked");
  constexpr uint8_t Step = UniformElementIndexOf(GlassUDFTentBlur, "Step");
  writer->set(Radius, std::array<float, 2>{params.tentFineRadius, params.tentCoarseRadius});
  writer->set(Field, params.tentField);
  writer->set(InputIsPacked, params.tentInputIsPacked);
  Point stepVectors[] = {{0, 0}, {1.0f, 0}};
  if (params.tentVertical) {
    stepVectors[1] = {0, 1.0f};
  }
  auto view = child != nullptr && child->textureProxy != nullptr
                  ? child->textureProxy->getTextureView()
                  : nullptr;
  if (child != nullptr) {
    auto matrix = child->uvMatrix;
    if (view != nullptr) {
      auto scale = view->getTextureCoord(1, 1);
      matrix.postScale(scale.x, scale.y);
      if (view->origin() == ImageOrigin::BottomLeft) {
        matrix.postScale(1, -1);
        auto translate = view->getTextureCoord(0, static_cast<float>(view->height()));
        matrix.postTranslate(translate.x, translate.y);
      }
    }
    matrix.mapPoints(stepVectors, 2);
  }
  const Point step = stepVectors[1] - stepVectors[0];
  writer->set(Step, std::array<float, 2>{step.x, step.y});
  WriteChildSamplingFields<GlassUDFTentBlur>(child, view.get(), writer);
}

void WriteGaussianBlur(const AOTOpaqueKernelParameters& params, const AOTTextureParameters* child,
                       SlotWriter* writer) {
  constexpr uint8_t Kernel = UniformElementIndexOf(GaussianBlur, "Kernel");
  constexpr uint8_t Radius = UniformElementIndexOf(GaussianBlur, "Radius");
  constexpr uint8_t Step = UniformElementIndexOf(GaussianBlur, "Step");
  constexpr uint8_t AlphaChild = UniformElementIndexOf(GaussianBlur, "AlphaChild");
  writer->set(Kernel, params.packedKernel);
  writer->set(Radius, params.kernelRadius);
  Point stepVectors[] = {{0, 0}, {params.stepLength, 0}};
  if (params.vertical) {
    stepVectors[1] = {0, params.stepLength};
  }
  auto view = child != nullptr && child->textureProxy != nullptr
                  ? child->textureProxy->getTextureView()
                  : nullptr;
  if (child != nullptr) {
    auto matrix = child->uvMatrix;
    if (view != nullptr) {
      auto scale = view->getTextureCoord(1, 1);
      matrix.postScale(scale.x, scale.y);
      if (view->origin() == ImageOrigin::BottomLeft) {
        matrix.postScale(1, -1);
        auto translate = view->getTextureCoord(0, static_cast<float>(view->height()));
        matrix.postTranslate(translate.x, translate.y);
      }
    }
    matrix.mapPoints(stepVectors, 2);
  }
  const Point step = stepVectors[1] - stepVectors[0];
  writer->set(Step, std::array<float, 2>{step.x, step.y});
  writer->set(AlphaChild, static_cast<int32_t>(child != nullptr && child->isAlphaOnly ? 1 : 0));
  WriteChildSamplingFields<GaussianBlur>(child, view.get(), writer);
}

// GlassRefraction: the optics and shape records, sourced from the IR node instead of the
// processor tree. The background-coordinate
// transform is the tree's first coord transform (the standard helper upload).
void WriteGlassRefraction(const AOTOpaqueKernelParameters& params, const ProgramInfo& info,
                          SlotWriter* writer) {
  constexpr uint8_t OpticsP0 = UniformElementIndexOf(GlassRefraction, "OpticsP0");
  constexpr uint8_t OpticsP1 = UniformElementIndexOf(GlassRefraction, "OpticsP1");
  constexpr uint8_t OpticsP2 = UniformElementIndexOf(GlassRefraction, "OpticsP2");
  constexpr uint8_t OpticsP3 = UniformElementIndexOf(GlassRefraction, "OpticsP3");
  constexpr uint8_t ShapeP0 = UniformElementIndexOf(GlassRefraction, "ShapeP0");
  constexpr uint8_t ShapeP1 = UniformElementIndexOf(GlassRefraction, "ShapeP1");
  constexpr uint8_t LightDir = UniformElementIndexOf(GlassRefraction, "LightDir");
  constexpr uint8_t LightingOn = UniformElementIndexOf(GlassRefraction, "LightingOn");
  // The UDF-only fields exist iff SHAPE == 2, so their writes take the ifPresent form.
  constexpr uint8_t FineMaskUV = UniformElementIndexOf(GlassRefraction, "FineMaskUV");
  constexpr uint8_t EdgeSpan = UniformElementIndexOf(GlassRefraction, "EdgeSpan");
  constexpr uint8_t EdgeMaskUV = UniformElementIndexOf(GlassRefraction, "EdgeMaskUV");
  constexpr uint8_t EdgeMaskOn = UniformElementIndexOf(GlassRefraction, "EdgeMaskOn");
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(GlassRefraction, "CoordMatrix");
  // The background transform: the record-time raw matrix through the shared total (the source
  // view supplies the normalization), with the geometry's uv matrix as the legacy walk did.
  const auto backgroundView =
      params.glassSourceProxy != nullptr ? params.glassSourceProxy->getTextureView() : nullptr;
  Matrix background = TotalCoordMatrix(params.glassBackgroundMatrix, backgroundView.get());
  const auto& geometry = info.getGeometryLayout().parameters;
  if (geometry.kind == GeometryKind::Default || geometry.kind == GeometryKind::QuadPerEdgeAA) {
    background.setConcat(background, geometry.uvMatrix);
  }
  writer->set(CoordMatrix, background);
  if (params.glassSourceProxy == nullptr) {
    LOGE("WriteGlassRefraction: the IR node carries no background source proxy");
    return;
  }
  const float sourceWidth = static_cast<float>(params.glassSourceProxy->width());
  const float sourceHeight = static_cast<float>(params.glassSourceProxy->height());
  writer->set(OpticsP0, std::array<float, 4>{1.0f / sourceWidth, 1.0f / sourceHeight,
                                             params.glassDispersion, params.glassMaxDisplacement});
  float layerToSourceX = params.glassLayerToSourceX;
  float layerToSourceY = params.glassLayerToSourceY;
  if (layerToSourceX <= 0.0f) {
    layerToSourceX = params.glassOrigWidth > 0.0f ? sourceWidth / params.glassOrigWidth : 1.0f;
  }
  if (layerToSourceY <= 0.0f) {
    layerToSourceY = params.glassOrigHeight > 0.0f ? sourceHeight / params.glassOrigHeight : 1.0f;
  }
  writer->set(OpticsP1,
              std::array<float, 2>{layerToSourceX / sourceWidth, layerToSourceY / sourceHeight});
  const float glassUVScaleX =
      params.glassUVScaleX > 0.0f ? params.glassUVScaleX : 1.0f / sourceWidth;
  const float glassUVScaleY =
      params.glassUVScaleY > 0.0f ? params.glassUVScaleY : 1.0f / sourceHeight;
  writer->set(OpticsP2,
              std::array<float, 4>{glassUVScaleX, glassUVScaleY, params.glassLightIntensity,
                                   params.glassFrost / 100.0f});
  writer->set(OpticsP3,
              std::array<float, 4>{params.glassUVOffsetX, params.glassUVOffsetY, 0.0f, 0.0f});
  writer->set(LightingOn, static_cast<int32_t>(params.glassLightIntensity > 0.0f ? 1 : 0));
  const auto& shape = params.glassShape;
  const float angle = shape.lightAngle * static_cast<float>(M_PI) / 180.0f;
  writer->set(LightDir, std::array<float, 2>{std::sin(angle), std::cos(angle)});
  writer->set(ShapeP1, std::array<float, 4>{shape.refractionFactor, shape.splay, shape.depthRatio,
                                            shape.edgeBandLayerPixels});
  if (params.geometryKind == AOTGlassGeometryKind::Udf) {
    writer->set(ShapeP0, std::array<float, 4>{shape.halfW, shape.halfH, shape.udfPixelToLayerPixelX,
                                              shape.udfPixelToLayerPixelY});
    writer->setIfPresent(EdgeSpan, std::array<float, 2>{shape.edgeSpanX, shape.edgeSpanY});
    if (shape.maskProxy == nullptr) {
      // The UDF child's constructor guarantees the mask; reaching here means a malformed IR.
      LOGE("WriteGlassRefraction: the UDF node carries no mask proxy");
      return;
    }
    const float textureWidth = static_cast<float>(shape.maskProxy->width());
    const float textureHeight = static_cast<float>(shape.maskProxy->height());
    const float coreWidth = std::round(shape.halfW * 2.0f / shape.udfPixelToLayerPixelX);
    const float coreHeight = std::round(shape.halfH * 2.0f / shape.udfPixelToLayerPixelY);
    writer->setIfPresent(FineMaskUV,
                         std::array<float, 4>{coreWidth / textureWidth, coreHeight / textureHeight,
                                              -shape.textureOriginX / textureWidth,
                                              -shape.textureOriginY / textureHeight});
    float edgeScaleX = 1.0f;
    float edgeScaleY = 1.0f;
    float edgeOriginX = shape.edgeTextureOriginX;
    float edgeOriginY = shape.edgeTextureOriginY;
    if (shape.enableEdgeLighting && shape.edgeMaskProxy != nullptr) {
      const float edgeTextureWidth = static_cast<float>(shape.edgeMaskProxy->width());
      const float edgeTextureHeight = static_cast<float>(shape.edgeMaskProxy->height());
      const float edgeCoreWidth = std::round(shape.halfW * 2.0f / shape.edgePixelToLayerPixelX);
      const float edgeCoreHeight = std::round(shape.halfH * 2.0f / shape.edgePixelToLayerPixelY);
      edgeScaleX = edgeCoreWidth / edgeTextureWidth;
      edgeScaleY = edgeCoreHeight / edgeTextureHeight;
      edgeOriginX = -shape.edgeTextureOriginX / edgeTextureWidth;
      edgeOriginY = -shape.edgeTextureOriginY / edgeTextureHeight;
    }
    writer->setIfPresent(EdgeMaskUV,
                         std::array<float, 4>{edgeScaleX, edgeScaleY, edgeOriginX, edgeOriginY});
    writer->setIfPresent(
        EdgeMaskOn,
        static_cast<int32_t>(shape.enableEdgeLighting && shape.edgeMaskProxy != nullptr ? 1 : 0));
  } else {
    writer->set(ShapeP0, std::array<float, 4>{shape.halfW, shape.halfH, shape.cornerRadius,
                                              shape.glassThickness});
  }
}

template <const Module& module>
void WriteClipParams(const ProgramInfo& info, SlotWriter* writer);

void WriteAnalyticClip(const ProgramInfo& info, SlotWriter* writer) {
  WriteClipParams<AnalyticClip>(info, writer);
}

// HairlineLineGeometry / HairlineQuadGeometry: the view matrix, the color/coverage/AA triple and
// the analytic clip the old declaration carried in the same block.
template <const Module& module>
void WriteHairlineGeometry(const ProgramInfo& info, const AOTGeometryParameters& params,
                           SlotWriter* writer) {
  constexpr uint8_t ViewMatrix = UniformElementIndexOf(module, "Matrix");
  constexpr uint8_t Color = UniformElementIndexOf(module, "Color");
  constexpr uint8_t Coverage = UniformElementIndexOf(module, "Coverage");
  constexpr uint8_t AAEnabled = UniformElementIndexOf(module, "AAEnabled");
  writer->set(ViewMatrix, params.viewMatrix);
  writer->set(Color, params.commonColor);
  writer->set(Coverage, params.coverage);
  writer->set(AAEnabled, static_cast<int32_t>(params.aaType == AAType::None ? 0 : 1));
  WriteClipParams<module>(info, writer);
}

// AnalyticClip: the runtime clip contract.
/// Writes the analytic clip parameters (slot's module fields) from the draw's IR analytic term.
/// The hairline families carry the same fields inside their geometry module, so the shared body
/// templates on the module that owns the fields.
template <const Module& module>
void WriteClipParams(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Rect = UniformElementIndexOf(module, "Rect");
  constexpr uint8_t HasClip = UniformElementIndexOf(module, "HasClip");
  constexpr uint8_t LocalRect = UniformElementIndexOf(module, "LocalRect");
  constexpr uint8_t RadiiX = UniformElementIndexOf(module, "RadiiX");
  constexpr uint8_t RadiiY = UniformElementIndexOf(module, "RadiiY");
  constexpr uint8_t AntiAlias = UniformElementIndexOf(module, "AntiAlias");
  constexpr uint8_t DeviceToLocal = UniformElementIndexOf(module, "DeviceToLocal");
  // The lowering already classified the clip into the IR's analytic term and copied every
  // parameter into its node chain (RectCoverage / RRectCoverage), so the writer reads the draw's
  // IR instead of searching the processor tree — the same values, sourced from the one place that
  // owns them.
  const AOTCoverageTerm* analytic = nullptr;
  const DrawIR* ir = info.getCarriedIR();
  if (ir != nullptr) {
    for (const auto& term : ir->coverage) {
      if (term.kind == AOTCoverageTermKind::Analytic) {
        analytic = &term;
        break;
      }
    }
  }
  if (analytic != nullptr) {
    for (size_t index = 0; index < analytic->graph.nodeCount(); ++index) {
      const auto* node =
          analytic->graph.nodeAt(static_cast<AOTNodeID>(static_cast<uint32_t>(index)));
      if (node == nullptr) {
        continue;
      }
      if (const auto* rect = std::get_if<AOTRectCoverageParameters>(&node->parameters)) {
        const Matrix deviceToLocal =
            Matrix::MakeAll(rect->deviceToLocal[0], rect->deviceToLocal[1], rect->deviceToLocal[2],
                            rect->deviceToLocal[3], rect->deviceToLocal[4], rect->deviceToLocal[5],
                            rect->deviceToLocal[6], rect->deviceToLocal[7], rect->deviceToLocal[8]);
        writer->set(LocalRect, std::array<float, 4>{rect->rect[0], rect->rect[1], rect->rect[2],
                                                    rect->rect[3]});
        // The RectCoverage node is the AA form: the lowering routes hard-edged rects through the
        // rrect node with a zero antiAlias.
        writer->set(AntiAlias, 1.0f);
        writer->set(DeviceToLocal, deviceToLocal);
        if (deviceToLocal == Matrix::I()) {
          // The device-space form uploads half a pixel outward so the coverage ramp centers on the
          // geometric boundary.
          writer->set(Rect, std::array<float, 4>{rect->rect[0] - 0.5f, rect->rect[1] - 0.5f,
                                                 rect->rect[2] + 0.5f, rect->rect[3] + 0.5f});
          writer->set(HasClip, static_cast<int32_t>(1));
        } else {
          writer->set(HasClip, static_cast<int32_t>(3));
        }
        return;
      }
      if (const auto* rrect = std::get_if<AOTRRectCoverageParameters>(&node->parameters)) {
        writer->set(LocalRect, std::array<float, 4>{rrect->rect[0], rrect->rect[1], rrect->rect[2],
                                                    rrect->rect[3]});
        writer->set(RadiiX, rrect->radiiX);
        writer->set(RadiiY, rrect->radiiY);
        writer->set(AntiAlias, rrect->antiAlias);
        writer->set(DeviceToLocal, Matrix::MakeAll(rrect->deviceToLocal[0], rrect->deviceToLocal[1],
                                                   rrect->deviceToLocal[2], rrect->deviceToLocal[3],
                                                   rrect->deviceToLocal[4], rrect->deviceToLocal[5],
                                                   rrect->deviceToLocal[6], rrect->deviceToLocal[7],
                                                   rrect->deviceToLocal[8]));
        writer->set(HasClip, static_cast<int32_t>(2));
        return;
      }
    }
  }
  // No clip: the contract branch reads nothing but the flag.
  writer->set(HasClip, static_cast<int32_t>(0));
}

// Xfer: the blend mode, the dst-texture coord mapping for the dst dialect, and the render-target
// write swizzle — the same values BindPorterDuffXfer uploads for the legacy families.
}  // namespace

/// The declaration-driven families' writer. The variant is fixed at construction: the layout is
/// derived once per program (not once per draw), and every field write addresses the element's
/// index in its module's declaration (a compile-time constant) through the layout's elementField
/// table — no name is composed at draw time. The per-draw work is one match rule (the program's
/// own family, for the data-source leaves) plus the module walk.
class DeclaredFamilyProgramWriter final : public PrecompiledProgramWriter {
 public:
  DeclaredFamilyProgramWriter(const ProgramInfo& info, Variant variant);

  bool canWrite(const ProgramInfo& info) const override;

 protected:
  void writeEngineDefaults(const ProgramInfo& info,
                           UniformData* fragmentUniformData) const override;
  void writeGeometryProcessor(const ProgramInfo& info, UniformData* vertexUniformData,
                              UniformData* fragmentUniformData) const override;
  void writeProcessors(const ProgramInfo& info, UniformData* vertexUniformData,
                       UniformData* fragmentUniformData) const override;

 private:
  Variant variant;
  Layout layout;
};

DeclaredFamilyProgramWriter::DeclaredFamilyProgramWriter([[maybe_unused]] const ProgramInfo& info,
                                                         Variant variant)
    : variant(std::move(variant)), layout(DeriveLayout(variant)) {
}

bool DeclaredFamilyProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

void DeclaredFamilyProgramWriter::writeEngineDefaults(const ProgramInfo&, UniformData*) const {
}

void DeclaredFamilyProgramWriter::writeGeometryProcessor(const ProgramInfo& info,
                                                         UniformData* vertexUniformData,
                                                         UniformData* fragmentUniformData) const {
  // The draw's selection ran once, before the program was created from this variant, and its data
  // sources (the texture leaves, the kernels) rode the info to here: the upload never re-matches.
  const auto* match = info.getSources();
  if (match == nullptr) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw carries no selection for the %s family; uniforms "
        "stay unwritten",
        Families()[variant.family].name);
    return;
  }
  // The program key carries the variant, so a draw landing on this program was selected for the
  // same variant; a mismatch means one key covers two variants and the leaves would write into
  // the wrong layout.
  if (match->variant.id() != variant.id()) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw selected %s variant %u but the program was built "
        "for %u — one program key covers two variants",
        Families()[variant.family].name, match->variant.id(), variant.id());
    return;
  }
  const Family& family = Families()[variant.family];
  const auto modules = ModulesOf(family, variant);
  const auto& geometryParams = info.getGeometryLayout().parameters;
  ParamBuffer buffer(vertexUniformData, fragmentUniformData);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    SlotWriter writer(layout, *module, slot, &buffer);
    // The migrated modules write through their own file; the rest still share the switch below.
    if (const auto writeModule = WriterOf(*module)) {
      WriteContext context = {info, *match, geometryParams, &writer};
      writeModule(context);
      continue;
    }
    if (module == &BlurSourceGeometry) {
      WriteBlurSourceGeometry(info, geometryParams, &writer);
    } else if (module == &HairlineLineGeometry) {
      WriteHairlineGeometry<HairlineLineGeometry>(info, geometryParams, &writer);
    } else if (module == &HairlineQuadGeometry) {
      WriteHairlineGeometry<HairlineQuadGeometry>(info, geometryParams, &writer);
    } else if (module == &MeshGeometry) {
      WriteMeshGeometry(info, geometryParams, &writer);
    } else if (module == &ShapeInstancedGeometry) {
      WriteShapeInstancedGeometry(info, geometryParams, &writer);
    } else if (module == &GaussianBlur) {
      if (match->blurKernel != nullptr) {
        WriteGaussianBlur(*match->blurKernel, match->blurChild, &writer);
      }
    } else if (module == &GlassUDFTentBlur) {
      if (match->blurKernel != nullptr) {
        WriteGlassUDFTentBlur(*match->blurKernel, match->blurChild, &writer);
      }
    } else if (module == &GlassRefraction) {
      if (match->glassKernel != nullptr) {
        WriteGlassRefraction(*match->glassKernel, info, &writer);
      }
    } else if (module == &GradientModule) {
      if (match->gradient != nullptr) {
        WriteGradient(*match->gradient, &writer);
      }
    } else if (module == &YUVChainTail) {
      // The analytic clip fields live in this module (bare names, the legacy spelling); the tail's
      // slot records come from the compiled program through the same binder the legacy kernel used.
      WriteClipParams<YUVChainTail>(info, &writer);
      if (const auto* program = AOTChainBuilder::ResolveYUVProgram(&info)) {
        BindYUVProgram(*program, &writer);
      }
    } else if (module == &AnalyticClip) {
      WriteAnalyticClip(info, &writer);
    }
  }
}

void DeclaredFamilyProgramWriter::writeProcessors(const ProgramInfo&, UniformData*,
                                                  UniformData*) const {
  // Every value write happens in writeGeometryProcessor's module walk.
}

std::unique_ptr<ProgramWriter> MakeDeclaredFamilyProgramWriter(const ProgramInfo& info,
                                                               const Variant& variant) {
  return std::make_unique<DeclaredFamilyProgramWriter>(info, variant);
}

}  // namespace tgfx
