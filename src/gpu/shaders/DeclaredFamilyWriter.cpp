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
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/Swizzle.h"
#include "gpu/UniformData.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComplexEllipseGeometryProcessor.h"
#include "gpu/processors/ComplexNonAARRectGeometryProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/ParamBuffer.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/shaders/modules/AnalyticClip.h"
#include "gpu/shaders/modules/AtlasTextGeometry.h"
#include "gpu/shaders/modules/BlurSourceGeometry.h"
#include "gpu/shaders/modules/ComplexEllipseGeometry.h"
#include "gpu/shaders/modules/ComplexRRectGeometry.h"
#include "gpu/shaders/modules/CoverageTexture.h"
#include "gpu/shaders/modules/DeviceMask.h"
#include "gpu/shaders/modules/DeviceMaskRuntime.h"
#include "gpu/shaders/modules/EllipseGeometry.h"
#include "gpu/shaders/modules/Frame.h"
#include "gpu/shaders/modules/GaussianBlur.h"
#include "gpu/shaders/modules/GlassLocalMask.h"
#include "gpu/shaders/modules/GlassRefraction.h"
#include "gpu/shaders/modules/GlassUDFTentBlur.h"
#include "gpu/shaders/modules/Gradient.h"
#include "gpu/shaders/modules/HairlineLineGeometry.h"
#include "gpu/shaders/modules/HairlineQuadGeometry.h"
#include "gpu/shaders/modules/LocalMask.h"
#include "gpu/shaders/modules/MaskTexture.h"
#include "gpu/shaders/modules/MeshGeometry.h"
#include "gpu/shaders/modules/QuadColorGeometry.h"
#include "gpu/shaders/modules/QuadGeometry.h"
#include "gpu/shaders/modules/QuadTextureGeometry.h"
#include "gpu/shaders/modules/RRectGeometry.h"
#include "gpu/shaders/modules/RectGeometry.h"
#include "gpu/shaders/modules/RoundStrokeRectGeometry.h"
#include "gpu/shaders/modules/ShapeInstancedGeometry.h"
#include "gpu/shaders/modules/Texture.h"
#include "gpu/shaders/modules/TiledTexture.h"
#include "gpu/shaders/modules/Xfer.h"
#include "gpu/shaders/modules/YUVTexture.h"

namespace tgfx {

namespace {

#if DEBUG
/// Whether the geometry's slot list declares the location (the pairing check's per-attribute
/// probe; a plain loop in place of the std::any_of lambda the coding rules exclude).
bool SlotIsDeclared(const std::vector<uint32_t>& slots, uint32_t location) {
  for (uint32_t slot : slots) {
    if (slot == location) {
      return true;
    }
  }
  return false;
}
#endif

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
  if (renderTarget->getContext()->backend() == Backend::WebGPU &&
      renderTarget->origin() == ImageOrigin::TopLeft) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
}

// Frame (slot 0): the render-target adjust vector.
void WriteFrame(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t RTAdjust = UniformElementIndexOf(Frame, "RTAdjust");
  writer->set(RTAdjust, GetRTAdjustArray(info.getRenderTarget()));
}

// RectGeometry (the DefaultGP geometry): the view transform and the paint color uniform. The
// per-vertex coverage rides the varying and needs no write.
void WriteRectGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Matrix = UniformElementIndexOf(RectGeometry, "Matrix");
  constexpr uint8_t Color = UniformElementIndexOf(RectGeometry, "Color");
  const auto* geometry = static_cast<const DefaultGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Matrix, geometry->getViewMatrix());
  writer->set(Color, geometry->getCommonColor());
}

// QuadGeometry (the QuadPerEdgeAA geometry): the paint color uniform; the position is already in
// device space, so there is no view transform.
void WriteQuadGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Color = UniformElementIndexOf(QuadGeometry, "Color");
  const auto* geometry =
      static_cast<const QuadPerEdgeAAGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Color, geometry->getCommonColor());
}

template <const Module& module>
void WriteCoordMatrix(const ProgramInfo& info, SlotWriter* writer);

/// The transform at the given position in the FP tree's coordinate-transform sequence; a negative
/// index selects the last one.
template <const Module& module>
void WriteCoordMatrixAt(const ProgramInfo& info, SlotWriter* writer, int index);

template <const Module& module>
void WriteTextureLeafParams(const AOTTextureParameters& leaf, SlotWriter* writer);

// ComplexRRectGeometry: the color pair. The corner data and the SDF run entirely on the varyings.
void WriteComplexRRectGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Color = UniformElementIndexOf(ComplexRRectGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(ComplexRRectGeometry, "HasCommonColor");
  const auto* geometry =
      static_cast<const ComplexNonAARRectGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// MeshGeometry: the view transform, the color pair and the texture coordinate chain.
void WriteMeshGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Matrix = UniformElementIndexOf(MeshGeometry, "Matrix");
  constexpr uint8_t Color = UniformElementIndexOf(MeshGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(MeshGeometry, "HasCommonColor");
  const auto* geometry = static_cast<const MeshGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Matrix, geometry->getViewMatrix());
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->getHasColors() ? 0 : 1));
  WriteCoordMatrix<MeshGeometry>(info, writer);
}

// ShapeInstancedGeometry: the local-coordinate recovery, the view transform and the sampling
// coordinate. The instance offset and the instance color ride the vertex attributes. The sampling
// transform is the LAST one in the tree: the color graph's transforms come first, so a draw whose
// only fragment processors are the color ones leaves this unwritten (the instanced fill family
// carries no processor at all), and a coverage texture lands on it.
void WriteShapeInstancedGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t UVMatrix = UniformElementIndexOf(ShapeInstancedGeometry, "UVMatrix");
  constexpr uint8_t ViewMatrix = UniformElementIndexOf(ShapeInstancedGeometry, "ViewMatrix");
  const auto* geometry =
      static_cast<const ShapeInstancedGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(UVMatrix, geometry->getUVMatrix());
  writer->set(ViewMatrix, geometry->getViewMatrix());
  WriteCoordMatrixAt<ShapeInstancedGeometry>(info, writer, -1);
}

// CoverageTexture: the leaf's subset rect and read gates, the same values BindTextureSourceLeaf
// uploads. The sample coordinate rides the geometry's own transform.
void WriteCoverageTexture(const AOTTextureParameters& leaf, SlotWriter* writer) {
  WriteTextureLeafParams<CoverageTexture>(leaf, writer);
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

void WriteBlurSourceGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t ViewMatrix = UniformElementIndexOf(BlurSourceGeometry, "Matrix");
  const auto* geometry = info.getGeometryProcessor();
  Matrix view = Matrix::I();
  if (IsProcessor<DefaultGeometryProcessor>(geometry)) {
    view = static_cast<const DefaultGeometryProcessor*>(geometry)->getViewMatrix();
  }
  writer->set(ViewMatrix, view);
  WriteCoordMatrix<BlurSourceGeometry>(info, writer);
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
  WriteCoordMatrixAt<GlassRefraction>(info, writer, 0);
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

// GlassLocalMask: the runtime gate (the legacy kernel's HasLocalMask branch) plus the mask's own
// coordinate transform — the second one in the tree, the same matrix the standard helper uploads.
void WriteGlassLocalMask(const ProgramInfo& info, SlotWriter* writer, bool hasMask) {
  constexpr uint8_t HasMask = UniformElementIndexOf(GlassLocalMask, "HasMask");
  writer->set(HasMask, static_cast<int32_t>(hasMask ? 1 : 0));
  WriteCoordMatrixAt<GlassLocalMask>(info, writer, 1);
}

// The source coordinate matrix every texture leaf shares: the FP tree's first coord transform
// with the geometry's uv matrix pre-concatenated (setTransformDataHelper's composition).
Matrix SourceCoordMatrix(const ProgramInfo& info) {
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = iter.next();
  Matrix uvMatrix = Matrix::I();
  const auto* geometry = info.getGeometryProcessor();
  if (IsProcessor<DefaultGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const DefaultGeometryProcessor*>(geometry)->getUVMatrix();
  } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry)->getUVMatrix();
  }
  Matrix combined = {};
  combined.setConcat(coordTransform != nullptr ? coordTransform->getTotalMatrix() : Matrix::I(),
                     uvMatrix);
  return combined;
}

// The leaf's subset rect and read gates, the same values BindTextureSourceLeaf uploads. The
// texture-carrying modules declare different subsets of these fields, so the shared body
// templates on the module and writes exactly the fields it declares (a compile-time fact).
template <const Module& module>
void WriteTextureLeafParams(const AOTTextureParameters& leaf, SlotWriter* writer) {
  constexpr uint8_t Subset = UniformElementIndexOf(module, "Subset");
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return;
  }
  float rect[4] = {};
  LeafSubsetRectForSampler(leaf.textureProxy, view.get(), leaf.subset, leaf.samplerState, rect);
  writer->set(Subset, std::array<float, 4>{rect[0], rect[1], rect[2], rect[3]});
  if constexpr (HasElement(module, "SubsetClampOn")) {
    constexpr uint8_t SubsetClampOn = UniformElementIndexOf(module, "SubsetClampOn");
    writer->set(SubsetClampOn,
                static_cast<int32_t>(
                    LeafSubsetClampOn(leaf.textureProxy, view.get(), leaf.subset) ? 1 : 0));
  }
  if constexpr (HasElement(module, "AlphaStart")) {
    constexpr uint8_t AlphaStart = UniformElementIndexOf(module, "AlphaStart");
    if (leaf.hasRGBAAA) {
      auto coord = view->getTextureCoord(leaf.alphaStart.x, leaf.alphaStart.y);
      writer->set(AlphaStart, std::array<float, 2>{coord.x, coord.y});
    }
  }
  if constexpr (HasElement(module, "AlphaOnly")) {
    constexpr uint8_t AlphaOnly = UniformElementIndexOf(module, "AlphaOnly");
    writer->set(AlphaOnly, static_cast<int32_t>(leaf.isAlphaOnly ? 1 : 0));
  }
  if constexpr (HasElement(module, "HasRgbaaa")) {
    constexpr uint8_t HasRgbaaa = UniformElementIndexOf(module, "HasRgbaaa");
    writer->set(HasRgbaaa, static_cast<int32_t>(leaf.hasRGBAAA ? 1 : 0));
  }
}

// AtlasTextGeometry: the atlas size scale, the color pair and the AlphaOnly switch. The atlas
// sample supplies the coverage itself, so the geometry needs no transform.
void WriteAtlasTextGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t AtlasSizeInv = UniformElementIndexOf(AtlasTextGeometry, "AtlasSizeInv");
  constexpr uint8_t Color = UniformElementIndexOf(AtlasTextGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(AtlasTextGeometry, "HasCommonColor");
  constexpr uint8_t AlphaOnly = UniformElementIndexOf(AtlasTextGeometry, "AlphaOnly");
  const auto* geometry =
      static_cast<const AtlasTextGeometryProcessor*>(info.getGeometryProcessor());
  const auto& proxy = geometry->getTextureProxy();
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view != nullptr && view->getTexture() != nullptr) {
    const auto scale = view->getTextureCoord(1.f, 1.f);
    writer->set(AtlasSizeInv, std::array<float, 2>{scale.x, scale.y});
  }
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
  writer->set(AlphaOnly, static_cast<int32_t>(geometry->isAlphaOnly() ? 1 : 0));
}

// QuadTextureGeometry: the paint color the texture source multiplies by.
void WriteQuadTextureGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Color = UniformElementIndexOf(QuadTextureGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(QuadTextureGeometry, "HasCommonColor");
  const auto* geometry =
      static_cast<const QuadPerEdgeAAGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// Texture: the plain leaf source. Both matrices carry the source coordinate transform — the
// subset bounds go through the same transform whether the geometry supplies uv coordinates or
// the position (the old kernel's two uniforms held the same value).
void WriteTexture(const ProgramInfo& info, const AOTTextureParameters& leaf, SlotWriter* writer) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(TextureModule, "CoordMatrix");
  constexpr uint8_t SubsetMatrix = UniformElementIndexOf(TextureModule, "SubsetMatrix");
  // The subset bounds go through the leaf's bare coord transform (the old kernel's
  // texSubsetMatrix), while the source coordinate takes the transform with the geometry's uv
  // matrix pre-concatenated — the two differ exactly when the geometry carries a uv matrix.
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = iter.next();
  const Matrix subsetMatrix =
      coordTransform != nullptr ? coordTransform->getTotalMatrix() : Matrix::I();
  writer->set(CoordMatrix, SourceCoordMatrix(info));
  writer->set(SubsetMatrix, subsetMatrix);
  WriteTextureLeafParams<TextureModule>(leaf, writer);
}

// LocalMask: the coverage mask's own coordinate transform, from the coverage TextureEffect.
void WriteLocalMask(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(LocalMask, "CoordMatrix");
  for (size_t index = info.numColorFragmentProcessors(); index < info.numFragmentProcessors();
       ++index) {
    const auto* fp = info.getFragmentProcessor(index);
    if (IsProcessor<TextureEffect>(fp)) {
      Matrix uvMatrix = Matrix::I();
      const auto* geometry = info.getGeometryProcessor();
      if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
        uvMatrix = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry)->getUVMatrix();
      }
      Matrix combined = {};
      combined.setConcat(
          static_cast<const TextureEffect*>(fp)->getCoordTransform().getTotalMatrix(), uvMatrix);
      writer->set(CoordMatrix, combined);
      return;
    }
  }
}

// DeviceMaskRuntime: the runtime gate plus the same device-space transform WriteDeviceMask
// computes; a draw without a device mask keeps the branch off.
void WriteDeviceMaskRuntime(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t HasDeviceMask = UniformElementIndexOf(DeviceMaskRuntime, "HasDeviceMask");
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(DeviceMaskRuntime, "CoordMatrix");
  const DeviceSpaceTextureEffect* mask = nullptr;
  for (size_t index = info.numColorFragmentProcessors(); index < info.numFragmentProcessors();
       ++index) {
    const auto* fp = info.getFragmentProcessor(index);
    if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
      mask = static_cast<const DeviceSpaceTextureEffect*>(fp);
    }
  }
  writer->set(HasDeviceMask, static_cast<int32_t>(mask != nullptr ? 1 : 0));
  if (mask == nullptr) {
    return;
  }
  auto deviceCoordMatrix = mask->getUVMatrix();
  const auto& proxy = mask->getTextureProxy();
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return;
  }
  const auto scale = view->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  writer->set(CoordMatrix, deviceCoordMatrix);
}

// YUVTexture: the source transform, the full-bounds subset and the conversion pair off the view.
void WriteYUVTexture(const ProgramInfo& info, const AOTTextureParameters& leaf,
                     SlotWriter* writer) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(YUVTexture, "CoordMatrix");
  constexpr uint8_t LimitedRange = UniformElementIndexOf(YUVTexture, "LimitedRange");
  constexpr uint8_t ColorConversion = UniformElementIndexOf(YUVTexture, "ColorConversion");
  writer->set(CoordMatrix, SourceCoordMatrix(info));
  WriteTextureLeafParams<YUVTexture>(leaf, writer);
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (view == nullptr || !view->isYUV()) {
    return;
  }
  auto* yuvView = static_cast<const YUVTextureView*>(view.get());
  writer->set(LimitedRange,
              static_cast<int32_t>(IsLimitedYUVColorRange(yuvView->yuvColorSpace()) ? 1 : 0));
  if (auto conversion = YUVColorConversionMatrix(yuvView->yuvColorSpace())) {
    writer->set(ColorConversion, *conversion);
  }
}

// QuadColorGeometry (the QuadPerEdgeAA geometry with the per-vertex color): the exact paint color
// and its flag. The per-vertex color rides the varying.
void WriteQuadColorGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Color = UniformElementIndexOf(QuadColorGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(QuadColorGeometry, "HasCommonColor");
  const auto* geometry =
      static_cast<const QuadPerEdgeAAGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// DeviceMask: the mask's destination-to-sample transform, the same value BindDeviceMaskLeaf
// uploads (the effect's uv matrix scaled by the texture view's normalized coord).
void WriteDeviceMask(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(DeviceMask, "CoordMatrix");
  const DeviceSpaceTextureEffect* mask = nullptr;
  for (size_t index = info.numColorFragmentProcessors();
       index < info.numFragmentProcessors() && mask == nullptr; ++index) {
    const auto* fp = info.getFragmentProcessor(index);
    if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
      mask = static_cast<const DeviceSpaceTextureEffect*>(fp);
    }
  }
  if (mask == nullptr) {
    return;
  }
  auto deviceCoordMatrix = mask->getUVMatrix();
  const auto& proxy = mask->getTextureProxy();
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view == nullptr || view->getTexture() == nullptr) {
    return;
  }
  const auto scale = view->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  writer->set(CoordMatrix, deviceCoordMatrix);
}

template <const Module& module>
void WriteClipParams(const ProgramInfo& info, SlotWriter* writer);

void WriteAnalyticClip(const ProgramInfo& info, SlotWriter* writer) {
  WriteClipParams<AnalyticClip>(info, writer);
}

// EllipseGeometry: the color pair and the stroke selector; the ellipse data rides the varyings.
void WriteEllipseGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Color = UniformElementIndexOf(EllipseGeometry, "Color");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(EllipseGeometry, "HasCommonColor");
  constexpr uint8_t StrokeEnabled = UniformElementIndexOf(EllipseGeometry, "StrokeEnabled");
  const auto* geometry = static_cast<const EllipseGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
  writer->set(StrokeEnabled, static_cast<int32_t>(geometry->isStroke() ? 1 : 0));
}

// ComplexEllipseGeometry: the color pair; the ellipse data rides the varyings.
void WriteComplexEllipseGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Color = UniformElementIndexOf(ComplexEllipseGeometry, "Color");
  constexpr uint8_t HasCommonColor =
      UniformElementIndexOf(ComplexEllipseGeometry, "HasCommonColor");
  const auto* geometry =
      static_cast<const ComplexEllipseGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
}

// RoundStrokeRectGeometry: the color pair, the AA flag and the analytic clip.
void WriteRoundStrokeRectGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t Color = UniformElementIndexOf(RoundStrokeRectGeometry, "Color");
  constexpr uint8_t HasCommonColor =
      UniformElementIndexOf(RoundStrokeRectGeometry, "HasCommonColor");
  constexpr uint8_t AAEnabled = UniformElementIndexOf(RoundStrokeRectGeometry, "AAEnabled");
  const auto* geometry =
      static_cast<const RoundStrokeRectGeometryProcessor*>(info.getGeometryProcessor());
  writer->set(Color, geometry->getCommonColor());
  writer->set(HasCommonColor, static_cast<int32_t>(geometry->hasCommonColor() ? 1 : 0));
  writer->set(AAEnabled, static_cast<int32_t>(geometry->getAAType() == AAType::None ? 0 : 1));
}

// HairlineLineGeometry / HairlineQuadGeometry: the view matrix, the color/coverage/AA triple and
// the analytic clip the old declaration carried in the same block.
template <typename Geometry, const Module& module>
void WriteHairlineGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t ViewMatrix = UniformElementIndexOf(module, "Matrix");
  constexpr uint8_t Color = UniformElementIndexOf(module, "Color");
  constexpr uint8_t Coverage = UniformElementIndexOf(module, "Coverage");
  constexpr uint8_t AAEnabled = UniformElementIndexOf(module, "AAEnabled");
  const auto* geometry = static_cast<const Geometry*>(info.getGeometryProcessor());
  writer->set(ViewMatrix, geometry->getViewMatrix());
  writer->set(Color, geometry->getCommonColor());
  writer->set(Coverage, geometry->getCoverage());
  writer->set(AAEnabled, static_cast<int32_t>(geometry->getAAType() == AAType::None ? 0 : 1));
  WriteClipParams<module>(info, writer);
}

// RRectGeometry (the non-AA rounded rect): the exact common color and its flag. The 8-bit
// quantized varying needs no write; the values come from the geometry processor.
void WriteRRectGeometry(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t CommonColor = UniformElementIndexOf(RRectGeometry, "CommonColor");
  constexpr uint8_t HasCommonColor = UniformElementIndexOf(RRectGeometry, "HasCommonColor");
  const auto* geometry =
      static_cast<const NonAARRectGeometryProcessor*>(info.getGeometryProcessor());
  const auto commonColor = geometry->getCommonColor();
  writer->set(CommonColor, commonColor.value_or(PMColor()));
  writer->set(HasCommonColor, static_cast<int32_t>(commonColor.has_value() ? 1 : 0));
}

/// The texture-space coordinate transform: the FP tree's coord transform at the given position
/// (local to normalized texture space), the same matrix setTransformDataHelper uploads for the
/// legacy families. A negative index selects the last transform, which is where the coverage
/// terms sit — the color graph's transforms come first. The IR's uvMatrix keeps the raw user
/// matrix, which does not carry the texture's normalization.
template <const Module& module>
void WriteCoordMatrixAt(const ProgramInfo& info, SlotWriter* writer, int index) {
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(module, "CoordMatrix");
  FragmentProcessor::CoordTransformIter iter(&info);
  const CoordTransform* coordTransform = nullptr;
  int position = 0;
  while (const CoordTransform* candidate = iter.next()) {
    if (index < 0) {
      coordTransform = candidate;
    } else {
      if (position == index) {
        coordTransform = candidate;
        break;
      }
      ++position;
    }
  }
  if (coordTransform == nullptr) {
    return;
  }
  // setTransformDataHelper equivalents concatenate the geometry's uv matrix onto the transform
  // (a masked path draw carries its mask offset there); the transform's own matrix alone leaves
  // the sample offset out and the coverage lands in the wrong place.
  Matrix uvMatrix = Matrix::I();
  const auto* geometry = info.getGeometryProcessor();
  if (IsProcessor<DefaultGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const DefaultGeometryProcessor*>(geometry)->getUVMatrix();
  } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
    uvMatrix = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry)->getUVMatrix();
  }
  Matrix combined = {};
  combined.setConcat(coordTransform->getTotalMatrix(), uvMatrix);
  writer->set(CoordMatrix, combined);
}

// The first transform's matrix: every family whose color source is the sole sampler reads it
// through this form.
template <const Module& module>
void WriteCoordMatrix(const ProgramInfo& info, SlotWriter* writer) {
  WriteCoordMatrixAt<module>(info, writer, 0);
}

// TiledTexture: the single-tap or full nine-mode recipe, field for field the same values
// BindTiledSourceLeaf uploads for the legacy families.
void WriteTiledTexture(const ProgramInfo& info, const AOTTextureParameters& leaf,
                       SlotWriter* writer) {
  constexpr uint8_t ShaderModeX = UniformElementIndexOf(TiledTexture, "ShaderModeX");
  constexpr uint8_t ShaderModeY = UniformElementIndexOf(TiledTexture, "ShaderModeY");
  constexpr uint8_t Subset = UniformElementIndexOf(TiledTexture, "Subset");
  constexpr uint8_t Clamp = UniformElementIndexOf(TiledTexture, "Clamp");
  constexpr uint8_t Dimension = UniformElementIndexOf(TiledTexture, "Dimension");
  constexpr uint8_t AlphaOnly = UniformElementIndexOf(TiledTexture, "AlphaOnly");
  constexpr uint8_t Strict = UniformElementIndexOf(TiledTexture, "Strict");
  // The runtime texture read swizzle (GRAY_8/RG_88) is only read by the full nine-mode path; its
  // field exists iff FULL != 0, absent from the single-tap variants. Same value
  // GLSLTiledTextureEffect uploads.
  constexpr uint8_t ReadSwizzle = UniformElementIndexOf(TiledTexture, "ReadSwizzle");
  WriteCoordMatrix<TiledTexture>(info, writer);
  if (!leaf.tiledRecipe.has_value()) {
    return;
  }
  const auto& sampling = *leaf.tiledRecipe;
  writer->set(ShaderModeX, static_cast<int32_t>(sampling.shaderModeX));
  writer->set(ShaderModeY, static_cast<int32_t>(sampling.shaderModeY));
  const float subset[4] = {sampling.shaderSubset.left, sampling.shaderSubset.top,
                           sampling.shaderSubset.right, sampling.shaderSubset.bottom};
  writer->set(Subset, subset);
  const float clampRect[4] = {sampling.shaderClamp.left, sampling.shaderClamp.top,
                              sampling.shaderClamp.right, sampling.shaderClamp.bottom};
  writer->set(Clamp, clampRect);
  if (sampling.usesShaderDimensions) {
    const float dimensions[2] = {sampling.shaderDimensions.x, sampling.shaderDimensions.y};
    writer->set(Dimension, dimensions);
  }
  writer->set(AlphaOnly, static_cast<int32_t>(sampling.alphaOnly ? 1 : 0));
  writer->set(Strict, static_cast<int32_t>(sampling.strict ? 1 : 0));
  if (writer->present(ReadSwizzle)) {
    auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
    if (view != nullptr && view->getTexture() != nullptr) {
      writer->set(ReadSwizzle,
                  static_cast<int32_t>(AOTReadSwizzleMode(view->getTexture()->format())));
    }
  }
}

// MaskTexture: the rasterized shape mask's coordinate transform. The texture itself is bound by
// the sampler path; the shader samples it and folds the red channel into the coverage.
void WriteMaskTexture(const ProgramInfo& info, SlotWriter* writer) {
  WriteCoordMatrix<MaskTexture>(info, writer);
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
void WriteXfer(const ProgramInfo& info, SlotWriter* writer) {
  constexpr uint8_t XPBlendMode = UniformElementIndexOf(Xfer, "XPBlendMode");
  constexpr uint8_t DstTextureUpperLeft = UniformElementIndexOf(Xfer, "DstTextureUpperLeft");
  constexpr uint8_t DstTextureCoordScale = UniformElementIndexOf(Xfer, "DstTextureCoordScale");
  constexpr uint8_t OutputAlphaSwizzle = UniformElementIndexOf(Xfer, "OutputAlphaSwizzle");
  // The write swizzle is unconditional (the HAS_XP == 0 path composes too).
  writer->set(OutputAlphaSwizzle,
              static_cast<int32_t>(info.getOutputSwizzle() == Swizzle::AAAA() ? 1 : 0));
  const auto* processor = info.getXferProcessor();
  // Only the PorterDuff processor carries a blend mode and a dst texture; the passthrough XP
  // (the HAS_XP == 0 variants) declares neither field.
  if (!IsProcessor<PorterDuffXferProcessor>(processor)) {
    return;
  }
  const auto* porterDuff = static_cast<const PorterDuffXferProcessor*>(processor);
  // The dst-dialect fields are `when HAS_XP != 0`: absent when the draw's xfer is the
  // passthrough, which the processor check above already excludes.
  writer->setIfPresent(XPBlendMode, static_cast<int32_t>(porterDuff->getBlendMode()));
  const auto& dstInfo = porterDuff->getDstTextureInfo();
  if (dstInfo.textureProxy == nullptr) {
    return;
  }
  auto dstView = dstInfo.textureProxy->getTextureView();
  if (dstView == nullptr || dstView->getTexture() == nullptr) {
    return;
  }
  const std::array<float, 2> upperLeft = {dstInfo.offset.x, dstInfo.offset.y};
  writer->setIfPresent(DstTextureUpperLeft, upperLeft);
  int width = dstView->width();
  int height = dstView->height();
  if (dstView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  }
  const std::array<float, 2> scales = {1.f / static_cast<float>(width),
                                       1.f / static_cast<float>(height)};
  writer->setIfPresent(DstTextureCoordScale, scales);
}

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
#if DEBUG
  // The pairing contract, spelled the way the program creator applies it: a shader that declares
  // one input per geometry attribute pairs in order (the count must match), and one that reads a
  // strict subset pairs by physical slot (every declared location must be a slot the geometry
  // actually provides). A violation here means the module's element order disagrees with the
  // geometry's declared attribute order, which would otherwise surface only as pixel damage.
  // Checked once per program, not once per draw.
  {
    const auto& geometryAttributes = info.getVertexAttributes();
    const auto& geometrySlots = info.getGeometryProcessor()->vertexAttributeLocations();
    // Instance inputs occupy the slots after the vertex ones (the creator's own split), so the
    // pairing checks below see the vertex inputs alone.
    uint32_t instanceBase = 0;
    for (auto slot : geometrySlots) {
      instanceBase = std::max(instanceBase, slot + 1);
    }
    size_t vertexInputCount = 0;
    for (const auto& attribute : layout.attributes) {
      if (attribute.location < instanceBase) {
        ++vertexInputCount;
      }
    }
    if (vertexInputCount > geometryAttributes.size()) {
      LOGE(
          "DeclaredFamilyProgramWriter: %s declares %zu vertex inputs but the geometry provides "
          "%zu attributes — the module's element list is out of sync with the geometry",
          Families()[variant.family].name, vertexInputCount, geometryAttributes.size());
      DEBUG_ASSERT(false);
    }
    // Only a strict subset pairs by physical slot (the creator parks the unread attributes); a
    // one-to-one interface pairs in order, where the declared locations are internal numbering.
    if (vertexInputCount < geometryAttributes.size()) {
      for (const auto& attribute : layout.attributes) {
        if (attribute.location >= instanceBase) {
          continue;
        }
        const bool onSlot = SlotIsDeclared(geometrySlots, attribute.location);
        if (!onSlot) {
          LOGE(
              "DeclaredFamilyProgramWriter: attribute %s wants location %u, which the geometry "
              "does not declare — the module's element order disagrees with the geometry's "
              "attribute order",
              attribute.name.c_str(), attribute.location);
          DEBUG_ASSERT(false);
        }
      }
    }
  }
#endif
}

bool DeclaredFamilyProgramWriter::canWrite(const ProgramInfo&) const {
  return true;
}

void DeclaredFamilyProgramWriter::writeEngineDefaults(const ProgramInfo&, UniformData*) const {
}

void DeclaredFamilyProgramWriter::writeGeometryProcessor(const ProgramInfo& info,
                                                         UniformData* vertexUniformData,
                                                         UniformData* fragmentUniformData) const {
  const auto shape = ExtractDrawShape(&info);
  // The program's own rule alone: its family and variant were fixed at construction, and the rule
  // is the only place the per-draw data sources (the texture leaves, the kernels) come from.
  FamilyMatchResult match = {};
  if (!FamilyMatchers()[variant.family](&info, shape, &match)) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw no longer matches the %s family; uniforms stay "
        "unwritten",
        Families()[variant.family].name);
    return;
  }
  // The program key carries the variant, so a draw landing on this program re-matches the same
  // variant; a mismatch means one key covers two variants and the leaves would write into the
  // wrong layout.
  if (match.variant.id() != variant.id()) {
    LOGE(
        "DeclaredFamilyProgramWriter: the draw matched %s variant %u but the program was built "
        "for %u — one program key covers two variants",
        Families()[variant.family].name, match.variant.id(), variant.id());
    return;
  }
  const Family& family = Families()[variant.family];
  const auto modules = ModulesOf(family, variant);
  ParamBuffer buffer(vertexUniformData, fragmentUniformData);
  for (uint8_t slot = 0; slot < modules.count; ++slot) {
    const Module* module = modules.list[slot];
    if (module == nullptr || !ModulePresent(family, variant, slot)) {
      continue;
    }
    SlotWriter writer(layout, *module, slot, &buffer);
    if (module == &FrameModule()) {
      WriteFrame(info, &writer);
    } else if (module == &RectGeometry) {
      WriteRectGeometry(info, &writer);
    } else if (module == &BlurSourceGeometry) {
      WriteBlurSourceGeometry(info, &writer);
    } else if (module == &RoundStrokeRectGeometry) {
      WriteRoundStrokeRectGeometry(info, &writer);
    } else if (module == &ComplexEllipseGeometry) {
      WriteComplexEllipseGeometry(info, &writer);
    } else if (module == &EllipseGeometry) {
      WriteEllipseGeometry(info, &writer);
    } else if (module == &HairlineLineGeometry) {
      WriteHairlineGeometry<HairlineLineGeometryProcessor, HairlineLineGeometry>(info, &writer);
    } else if (module == &HairlineQuadGeometry) {
      WriteHairlineGeometry<HairlineQuadGeometryProcessor, HairlineQuadGeometry>(info, &writer);
    } else if (module == &ComplexRRectGeometry) {
      WriteComplexRRectGeometry(info, &writer);
    } else if (module == &MeshGeometry) {
      WriteMeshGeometry(info, &writer);
    } else if (module == &ShapeInstancedGeometry) {
      WriteShapeInstancedGeometry(info, &writer);
    } else if (module == &QuadColorGeometry) {
      WriteQuadColorGeometry(info, &writer);
    } else if (module == &QuadGeometry) {
      WriteQuadGeometry(info, &writer);
    } else if (module == &RRectGeometry) {
      WriteRRectGeometry(info, &writer);
    } else if (module == &AtlasTextGeometry) {
      WriteAtlasTextGeometry(info, &writer);
    } else if (module == &QuadTextureGeometry) {
      WriteQuadTextureGeometry(info, &writer);
    } else if (module == &TextureModule) {
      if (match.tiledLeaf != nullptr) {
        WriteTexture(info, *match.tiledLeaf, &writer);
      }
    } else if (module == &CoverageTexture) {
      if (match.maskLeaf != nullptr) {
        WriteCoverageTexture(*match.maskLeaf, &writer);
      }
    } else if (module == &GaussianBlur) {
      if (match.blurKernel != nullptr) {
        WriteGaussianBlur(*match.blurKernel, match.blurChild, &writer);
      }
    } else if (module == &GlassUDFTentBlur) {
      if (match.blurKernel != nullptr) {
        WriteGlassUDFTentBlur(*match.blurKernel, match.blurChild, &writer);
      }
    } else if (module == &GlassRefraction) {
      if (match.glassKernel != nullptr) {
        WriteGlassRefraction(*match.glassKernel, info, &writer);
      }
    } else if (module == &GlassLocalMask) {
      WriteGlassLocalMask(info, &writer, match.maskLeaf != nullptr);
    } else if (module == &GradientModule) {
      if (match.gradient != nullptr) {
        WriteGradient(*match.gradient, &writer);
      }
    } else if (module == &LocalMask) {
      WriteLocalMask(info, &writer);
    } else if (module == &DeviceMaskRuntime) {
      WriteDeviceMaskRuntime(info, &writer);
    } else if (module == &YUVTexture) {
      if (match.tiledLeaf != nullptr) {
        WriteYUVTexture(info, *match.tiledLeaf, &writer);
      }
    } else if (module == &YUVChainTail) {
      // The analytic clip fields live in this module (bare names, the legacy spelling); the tail's
      // slot records come from the compiled program through the same binder the legacy kernel used.
      WriteClipParams<YUVChainTail>(info, &writer);
      if (const auto* program = AOTChainBuilder::ResolveYUVProgram(&info)) {
        BindYUVProgram(*program, fragmentUniformData);
      }
    } else if (module == &TiledTexture) {
      if (match.tiledLeaf != nullptr) {
        WriteTiledTexture(info, *match.tiledLeaf, &writer);
      }
    } else if (module == &DeviceMask) {
      WriteDeviceMask(info, &writer);
    } else if (module == &MaskTexture) {
      WriteMaskTexture(info, &writer);
    } else if (module == &AnalyticClip) {
      WriteAnalyticClip(info, &writer);
    } else if (module == &XferModule()) {
      WriteXfer(info, &writer);
    }
  }
}

void DeclaredFamilyProgramWriter::writeProcessors(const ProgramInfo&, UniformData*,
                                                  UniformData*) const {
  // Every value write happens in writeGeometryProcessor's module walk.
}

std::unique_ptr<ProgramWriter> MakeDeclaredFamilyProgramWriter(const ProgramInfo& info,
                                                               uint16_t familyIndex,
                                                               uint32_t permutationOrdinal) {
  auto variant = DecodeVariant(familyIndex, permutationOrdinal);
  if (!variant.has_value()) {
    LOGE("MakeDeclaredFamilyProgramWriter: %s has no variant %u in the family table",
         Families()[familyIndex].name, permutationOrdinal);
    return nullptr;
  }
  return std::make_unique<DeclaredFamilyProgramWriter>(info, *variant);
}

}  // namespace tgfx
