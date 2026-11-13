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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/write/modules/Effects.h"
#include <cmath>
#include "gpu/CoordTransform.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/shaders/modules/GaussianBlur.h"
#include "gpu/shaders/modules/GlassRefraction.h"
#include "gpu/shaders/modules/GlassUDFTentBlur.h"
#include "gpu/shaders/modules/Gradient.h"

namespace tgfx {

namespace {

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

/// The child's sampling step: the record-time direction mapped through the child leaf's raw
/// matrix, the instantiated view's normalization and (for a bottom-left origin) the origin flip.
Point ChildStep(const AOTTextureParameters* child, const Point& recordStep) {
  Point stepVectors[] = {{0, 0}, recordStep};
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
  return stepVectors[1] - stepVectors[0];
}

}  // namespace

// Gradient: the clamped single-interval gradient's whole record — the endpoints, the boundary
// colors, the layout branch and the conic layout's bias/scale pair.
void WriteGradientModule(WriteContext& context) {
  const auto* gradient = context.sources.gradient;
  if (gradient == nullptr) {
    return;
  }
  constexpr uint8_t CoordMatrix = UniformElementIndexOf(GradientModule, "CoordMatrix");
  constexpr uint8_t LeftBorderColor = UniformElementIndexOf(GradientModule, "LeftBorderColor");
  constexpr uint8_t RightBorderColor = UniformElementIndexOf(GradientModule, "RightBorderColor");
  constexpr uint8_t LayoutType = UniformElementIndexOf(GradientModule, "LayoutType");
  constexpr uint8_t Bias = UniformElementIndexOf(GradientModule, "Bias");
  constexpr uint8_t Scale = UniformElementIndexOf(GradientModule, "Scale");
  constexpr uint8_t Start = UniformElementIndexOf(GradientModule, "Start");
  constexpr uint8_t End = UniformElementIndexOf(GradientModule, "End");
  context.slot->set(CoordMatrix, gradient->coordMatrix);
  context.slot->set(LeftBorderColor, gradient->leftBorder);
  context.slot->set(RightBorderColor, gradient->rightBorder);
  context.slot->set(LayoutType, static_cast<int32_t>(gradient->layoutType));
  // The conic layout is the only one the shared gradient layout maths reads these two for; the
  // other layouts record their defaults, which the shader never touches.
  context.slot->set(Bias, gradient->bias);
  context.slot->set(Scale, gradient->scale);
  context.slot->set(Start, gradient->start);
  context.slot->set(End, gradient->end);
}

// GlassUDFTentBlur: the per-field radii, the field selector, the packed-input flag and the step.
void WriteGlassUDFTentBlurModule(WriteContext& context) {
  const auto* params = context.sources.blurKernel;
  if (params == nullptr) {
    return;
  }
  const auto* child = context.sources.blurChild;
  constexpr uint8_t Radius = UniformElementIndexOf(GlassUDFTentBlur, "Radius");
  constexpr uint8_t Field = UniformElementIndexOf(GlassUDFTentBlur, "Field");
  constexpr uint8_t InputIsPacked = UniformElementIndexOf(GlassUDFTentBlur, "InputIsPacked");
  constexpr uint8_t Step = UniformElementIndexOf(GlassUDFTentBlur, "Step");
  context.slot->set(Radius, std::array<float, 2>{params->tentFineRadius, params->tentCoarseRadius});
  context.slot->set(Field, params->tentField);
  context.slot->set(InputIsPacked, params->tentInputIsPacked);
  const Point step = ChildStep(child, params->tentVertical ? Point(0, 1.0f) : Point(1.0f, 0));
  context.slot->set(Step, std::array<float, 2>{step.x, step.y});
  auto view = child != nullptr && child->textureProxy != nullptr
                  ? child->textureProxy->getTextureView()
                  : nullptr;
  WriteChildSamplingFields<GlassUDFTentBlur>(child, view.get(), context.slot);
}

// GaussianBlur: the packed kernel table, its radius, the step's vector and the alpha-child flag.
void WriteGaussianBlurModule(WriteContext& context) {
  const auto* params = context.sources.blurKernel;
  if (params == nullptr) {
    return;
  }
  const auto* child = context.sources.blurChild;
  constexpr uint8_t Kernel = UniformElementIndexOf(GaussianBlur, "Kernel");
  constexpr uint8_t Radius = UniformElementIndexOf(GaussianBlur, "Radius");
  constexpr uint8_t Step = UniformElementIndexOf(GaussianBlur, "Step");
  constexpr uint8_t AlphaChild = UniformElementIndexOf(GaussianBlur, "AlphaChild");
  context.slot->set(Kernel, params->packedKernel);
  context.slot->set(Radius, params->kernelRadius);
  const Point recordStep =
      params->vertical ? Point(0, params->stepLength) : Point(params->stepLength, 0);
  const Point step = ChildStep(child, recordStep);
  context.slot->set(Step, std::array<float, 2>{step.x, step.y});
  context.slot->set(AlphaChild,
                    static_cast<int32_t>(child != nullptr && child->isAlphaOnly ? 1 : 0));
  auto view = child != nullptr && child->textureProxy != nullptr
                  ? child->textureProxy->getTextureView()
                  : nullptr;
  WriteChildSamplingFields<GaussianBlur>(child, view.get(), context.slot);
}

// GlassRefraction: the optics and shape records, sourced from the IR node instead of the processor
// tree. The background-coordinate transform is the record-time matrix through the shared total.
void WriteGlassRefractionModule(WriteContext& context) {
  const auto* params = context.sources.glassKernel;
  if (params == nullptr) {
    return;
  }
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
  const auto backgroundView =
      params->glassSourceProxy != nullptr ? params->glassSourceProxy->getTextureView() : nullptr;
  Matrix background = TotalCoordMatrix(params->glassBackgroundMatrix, backgroundView.get());
  const auto& geometry = context.info.getGeometryLayout().parameters;
  if (geometry.kind == GeometryKind::Default || geometry.kind == GeometryKind::QuadPerEdgeAA) {
    background.setConcat(background, geometry.uvMatrix);
  }
  context.slot->set(CoordMatrix, background);
  if (params->glassSourceProxy == nullptr) {
    LOGE("WriteGlassRefraction: the IR node carries no background source proxy");
    return;
  }
  const float sourceWidth = static_cast<float>(params->glassSourceProxy->width());
  const float sourceHeight = static_cast<float>(params->glassSourceProxy->height());
  context.slot->set(OpticsP0,
                    std::array<float, 4>{1.0f / sourceWidth, 1.0f / sourceHeight,
                                         params->glassDispersion, params->glassMaxDisplacement});
  float layerToSourceX = params->glassLayerToSourceX;
  float layerToSourceY = params->glassLayerToSourceY;
  if (layerToSourceX <= 0.0f) {
    layerToSourceX = params->glassOrigWidth > 0.0f ? sourceWidth / params->glassOrigWidth : 1.0f;
  }
  if (layerToSourceY <= 0.0f) {
    layerToSourceY = params->glassOrigHeight > 0.0f ? sourceHeight / params->glassOrigHeight : 1.0f;
  }
  context.slot->set(
      OpticsP1, std::array<float, 2>{layerToSourceX / sourceWidth, layerToSourceY / sourceHeight});
  const float glassUVScaleX =
      params->glassUVScaleX > 0.0f ? params->glassUVScaleX : 1.0f / sourceWidth;
  const float glassUVScaleY =
      params->glassUVScaleY > 0.0f ? params->glassUVScaleY : 1.0f / sourceHeight;
  context.slot->set(OpticsP2,
                    std::array<float, 4>{glassUVScaleX, glassUVScaleY, params->glassLightIntensity,
                                         params->glassFrost / 100.0f});
  context.slot->set(
      OpticsP3, std::array<float, 4>{params->glassUVOffsetX, params->glassUVOffsetY, 0.0f, 0.0f});
  context.slot->set(LightingOn, static_cast<int32_t>(params->glassLightIntensity > 0.0f ? 1 : 0));
  const auto& shape = params->glassShape;
  const float angle = shape.lightAngle * static_cast<float>(M_PI) / 180.0f;
  context.slot->set(LightDir, std::array<float, 2>{std::sin(angle), std::cos(angle)});
  context.slot->set(ShapeP1, std::array<float, 4>{shape.refractionFactor, shape.splay,
                                                  shape.depthRatio, shape.edgeBandLayerPixels});
  if (params->geometryKind == AOTGlassGeometryKind::Udf) {
    context.slot->set(ShapeP0,
                      std::array<float, 4>{shape.halfW, shape.halfH, shape.udfPixelToLayerPixelX,
                                           shape.udfPixelToLayerPixelY});
    context.slot->setIfPresent(EdgeSpan, std::array<float, 2>{shape.edgeSpanX, shape.edgeSpanY});
    if (shape.maskProxy == nullptr) {
      // The UDF child's constructor guarantees the mask; reaching here means a malformed IR.
      LOGE("WriteGlassRefraction: the UDF node carries no mask proxy");
      return;
    }
    const float textureWidth = static_cast<float>(shape.maskProxy->width());
    const float textureHeight = static_cast<float>(shape.maskProxy->height());
    const float coreWidth = std::round(shape.halfW * 2.0f / shape.udfPixelToLayerPixelX);
    const float coreHeight = std::round(shape.halfH * 2.0f / shape.udfPixelToLayerPixelY);
    context.slot->setIfPresent(
        FineMaskUV, std::array<float, 4>{coreWidth / textureWidth, coreHeight / textureHeight,
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
    context.slot->setIfPresent(
        EdgeMaskUV, std::array<float, 4>{edgeScaleX, edgeScaleY, edgeOriginX, edgeOriginY});
    context.slot->setIfPresent(
        EdgeMaskOn,
        static_cast<int32_t>(shape.enableEdgeLighting && shape.edgeMaskProxy != nullptr ? 1 : 0));
  } else {
    context.slot->set(ShapeP0, std::array<float, 4>{shape.halfW, shape.halfH, shape.cornerRadius,
                                                    shape.glassThickness});
  }
}

ModuleWriter EffectsWriterOf(const Module& module) {
  if (&module == &GradientModule) {
    return WriteGradientModule;
  }
  if (&module == &GaussianBlur) {
    return WriteGaussianBlurModule;
  }
  if (&module == &GlassUDFTentBlur) {
    return WriteGlassUDFTentBlurModule;
  }
  if (&module == &GlassRefraction) {
    return WriteGlassRefractionModule;
  }
  return nullptr;
}

}  // namespace tgfx
