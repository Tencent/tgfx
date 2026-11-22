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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/plan/ChainBindingWriter.h"
#include <array>
#include <cstring>
#include <vector>
#include "core/utils/Log.h"
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/GlobalCache.h"
#include "gpu/Program.h"
#include "gpu/ProgramInfo.h"
#include "gpu/SamplerState.h"
#include "gpu/Swizzle.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/ir/ChainLayoutMath.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/ir/Program.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
namespace {

// Everything the readers address: the draw's selection, the compiled program's binding table, the
// geometry facts and the coordinate transforms the stored vertex stage consumes. The operator
// parameters ride the graph nodes the plan's members carry, so no intermediate instruction
// encoding exists at this layer.
struct ReadContext {
  const ProgramInfo* info = nullptr;
  const AOTProgram* program = nullptr;
  const AOTGeometryParameters* geometry = nullptr;
  /// The emit's own binding tables (the leaf slots the body samples feed the swizzle reader).
  const ChainBindings* table = nullptr;
  std::vector<CoordTransform> transforms = {};
};

template <typename T>
void Store(uint8_t* out, const T& value) {
  std::memcpy(out, &value, sizeof(T));
}

// The std140 mat3 spelling: three vec4 columns, each column's fourth word padding.
void StoreMatrix3(uint8_t* out, const Matrix& matrix) {
  float values[9] = {};
  matrix.get9(values);
  const float data[12] = {values[0], values[3], values[6], 0.0f,      values[1], values[4],
                          values[7], 0.0f,      values[2], values[5], values[8], 0.0f};
  std::memcpy(out, data, sizeof(data));
}

// The row-major 9-element matrix the coverage parameters carry, in the same column-major spelling.
void StoreMatrix3(uint8_t* out, const std::array<float, 9>& matrix) {
  const float data[12] = {matrix[0], matrix[3], matrix[6], 0.0f,      matrix[1], matrix[4],
                          matrix[7], 0.0f,      matrix[2], matrix[5], matrix[8], 0.0f};
  std::memcpy(out, data, sizeof(data));
}

const AOTBinding* LeafBindingOf(const ReadContext& ctx, int leaf) {
  if (leaf < 0 || static_cast<size_t>(leaf) >= ctx.program->bindings.size()) {
    return nullptr;
  }
  return &ctx.program->bindings[static_cast<size_t>(leaf)];
}

const AOTBinding* MaskBindingOf(const ReadContext& ctx) {
  for (const auto& binding : ctx.program->bindings) {
    if (binding.kind == AOTBinding::Kind::Mask) {
      return &binding;
    }
  }
  return nullptr;
}

// The base matrix each geometry scheme concatenates onto the leaves' transforms, matching the
// declared geometry writer: the rect layouts apply their uv matrix, the mesh samples in its own
// space.
Matrix TransformBaseMatrix(const ReadContext& ctx) {
  const auto& parameters = *ctx.geometry;
  if (parameters.kind == GeometryKind::Mesh) {
    return Matrix::I();
  }
  return parameters.uvMatrix;
}

// One node's parameter block, typed: a member the walk registered addresses its operator's graph
// node, so the reader resolves the parameters off the node itself.
template <typename Parameters>
const Parameters* NodeParameters(const ChainBinding& binding, AOTEffectKind kind) {
  if (binding.node == nullptr || binding.node->kind != kind) {
    return nullptr;
  }
  return std::get_if<Parameters>(&binding.node->parameters);
}

bool ReadSlotColorMatrix(const ChainBinding& binding, bool linear, uint8_t* out) {
  const auto* parameters =
      NodeParameters<AOTColorMatrixParameters>(binding, AOTEffectKind::ColorMatrix);
  if (parameters == nullptr) {
    return false;
  }
  float linearData[16] = {};
  float translate[4] = {};
  PackAOTColorMatrix(parameters->matrix, linearData, translate);
  if (linear) {
    std::memcpy(out, linearData, sizeof(linearData));
  } else {
    std::memcpy(out, translate, sizeof(translate));
  }
  return true;
}

bool ReadLeafRecord(const ReadContext& ctx, int leaf, ChainBindingSource source, uint8_t* out) {
  const auto* binding = LeafBindingOf(ctx, leaf);
  if (binding == nullptr) {
    return false;
  }
  const auto view =
      binding->textureProxy != nullptr ? binding->textureProxy->getTextureView() : nullptr;
  if (view == nullptr) {
    return false;
  }
  switch (source) {
    case ChainBindingSource::LeafSubset: {
      const SamplerState sampling(binding->args.tileModeX, binding->args.tileModeY,
                                  binding->args.sampling);
      float rect[4] = {};
      LeafSubsetRectForSampler(binding->textureProxy, view.get(), binding->args.sampleArea,
                               sampling, rect);
      std::memcpy(out, rect, sizeof(rect));
      return true;
    }
    case ChainBindingSource::LeafSubsetClampOn: {
      const int clampOn = binding->args.sampleArea.has_value() ? 1 : 0;
      Store(out, clampOn);
      return true;
    }
    case ChainBindingSource::TiledShaderModeX:
      if (!binding->tiledRecipe.has_value()) {
        return false;
      }
      Store(out, static_cast<int>(binding->tiledRecipe->shaderModeX));
      return true;
    case ChainBindingSource::TiledShaderModeY:
      if (!binding->tiledRecipe.has_value()) {
        return false;
      }
      Store(out, static_cast<int>(binding->tiledRecipe->shaderModeY));
      return true;
    case ChainBindingSource::TiledSubset: {
      if (!binding->tiledRecipe.has_value()) {
        return false;
      }
      const float rect[4] = {
          binding->tiledRecipe->shaderSubset.left, binding->tiledRecipe->shaderSubset.top,
          binding->tiledRecipe->shaderSubset.right, binding->tiledRecipe->shaderSubset.bottom};
      std::memcpy(out, rect, sizeof(rect));
      return true;
    }
    case ChainBindingSource::TiledClamp: {
      if (!binding->tiledRecipe.has_value()) {
        return false;
      }
      const float rect[4] = {
          binding->tiledRecipe->shaderClamp.left, binding->tiledRecipe->shaderClamp.top,
          binding->tiledRecipe->shaderClamp.right, binding->tiledRecipe->shaderClamp.bottom};
      std::memcpy(out, rect, sizeof(rect));
      return true;
    }
    case ChainBindingSource::TiledDimension: {
      if (!binding->tiledRecipe.has_value()) {
        return false;
      }
      const float size[2] = {binding->tiledRecipe->shaderDimensions.x,
                             binding->tiledRecipe->shaderDimensions.y};
      std::memcpy(out, size, sizeof(size));
      return true;
    }
    case ChainBindingSource::TiledStrict:
      if (!binding->tiledRecipe.has_value()) {
        return false;
      }
      Store(out, binding->tiledRecipe->strict ? 1 : 0);
      return true;
    case ChainBindingSource::LeafAlphaStart: {
      if (binding->alphaStart == Point::Zero()) {
        return false;
      }
      const auto coord = view->getTextureCoord(binding->alphaStart.x, binding->alphaStart.y);
      const float alphaStart[2] = {coord.x, coord.y};
      std::memcpy(out, alphaStart, sizeof(alphaStart));
      return true;
    }
    case ChainBindingSource::LeafHasRgbaaa: {
      const int hasRgbaaa = binding->alphaStart != Point::Zero() ? 1 : 0;
      Store(out, hasRgbaaa);
      return true;
    }
    default:
      return false;
  }
}

bool ReadGradient(const ReadContext& ctx, const ChainBinding& binding, ChainBindingSource source,
                  int index, uint8_t* out) {
  const auto* gradient =
      NodeParameters<AOTGradientParameters>(binding, AOTEffectKind::GradientSource);
  if (gradient == nullptr) {
    return false;
  }
  switch (source) {
    case ChainBindingSource::GradientLayoutType:
      Store(out, gradient->layoutType);
      return true;
    case ChainBindingSource::GradientColorizerKind:
      Store(out, gradient->colorizerKind);
      return true;
    case ChainBindingSource::GradientLutLeaf:
      Store(out, ctx.program->lutLeafIndex);
      return true;
    case ChainBindingSource::GradientLeftBorder:
      std::memcpy(out, gradient->leftBorder.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientRightBorder:
      std::memcpy(out, gradient->rightBorder.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientStart:
      std::memcpy(out, gradient->start.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientEnd:
      std::memcpy(out, gradient->end.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientScale01:
      std::memcpy(out, gradient->scale01.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientBias01:
      std::memcpy(out, gradient->bias01.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientScale23:
      std::memcpy(out, gradient->scale23.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientBias23:
      std::memcpy(out, gradient->bias23.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientThreshold:
      Store(out, gradient->threshold);
      return true;
    case ChainBindingSource::GradientBias:
      Store(out, gradient->bias);
      return true;
    case ChainBindingSource::GradientScale:
      Store(out, gradient->scale);
      return true;
    case ChainBindingSource::GradientThresholds1_7:
      std::memcpy(out, gradient->thresholds1_7.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientThresholds9_13:
      std::memcpy(out, gradient->thresholds9_13.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientScales:
      if (index < 0 || index >= 8) {
        return false;
      }
      std::memcpy(out, gradient->scales[static_cast<size_t>(index)].data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientBiases:
      if (index < 0 || index >= 8) {
        return false;
      }
      std::memcpy(out, gradient->biases[static_cast<size_t>(index)].data(), sizeof(float) * 4);
      return true;
    default:
      return false;
  }
}

bool ReadColorSpace(const ChainBinding& binding, ChainBindingSource source, uint8_t* out) {
  const auto* parameters =
      NodeParameters<AOTColorSpaceXformParameters>(binding, AOTEffectKind::ColorSpaceXform);
  if (parameters == nullptr || parameters->steps == nullptr) {
    return false;
  }
  ColorSpaceXformValues values = {};
  ResolveColorSpaceXformValues(parameters->steps.get(), &values);
  switch (source) {
    case ChainBindingSource::ChainFlags:
      Store(out, values.flags);
      return true;
    case ChainBindingSource::ChainSrcTF0:
      std::memcpy(out, values.srcTF0, sizeof(values.srcTF0));
      return true;
    case ChainBindingSource::ChainSrcTF1:
      std::memcpy(out, values.srcTF1, sizeof(values.srcTF1));
      return true;
    case ChainBindingSource::ChainSrcTFType:
      Store(out, values.srcTFType);
      return true;
    case ChainBindingSource::ChainSrcOOTF:
      std::memcpy(out, values.srcOOTF, sizeof(values.srcOOTF));
      return true;
    case ChainBindingSource::ChainColorXform:
      std::memcpy(out, values.colorXform, sizeof(values.colorXform));
      return true;
    case ChainBindingSource::ChainDstOOTF:
      std::memcpy(out, values.dstOOTF, sizeof(values.dstOOTF));
      return true;
    case ChainBindingSource::ChainDstTF0:
      std::memcpy(out, values.dstTF0, sizeof(values.dstTF0));
      return true;
    case ChainBindingSource::ChainDstTF1:
      std::memcpy(out, values.dstTF1, sizeof(values.dstTF1));
      return true;
    case ChainBindingSource::ChainDstTFType:
      Store(out, values.dstTFType);
      return true;
    default:
      return false;
  }
}

// The multi-plane view of a YUV leaf's proxy, or null when the proxy carries no YUV view (a lazy
// or foreign proxy is not an error at this layer; the caller declines the member).
const YUVTextureView* YUVViewOf(const std::shared_ptr<TextureProxy>& proxy) {
  auto view = proxy != nullptr ? proxy->getTextureView() : nullptr;
  if (view == nullptr || !view->isYUV()) {
    return nullptr;
  }
  return static_cast<const YUVTextureView*>(view.get());
}

// The YUV source's members: the structural facts come off the multi-plane node's own parameters
// and the leaf binding at the site's own index (the source's subset and sampler state — the
// multi-plane source owns it); the conversion matrix and the limited-range flag resolve off the
// view here, at upload time — the same read the runtime path makes, so a lazy view and the
// runtime's own values agree.
bool ReadYUV(const ReadContext& ctx, const ChainBinding& binding, ChainBindingSource source,
             uint8_t* out) {
  const auto* parameters =
      NodeParameters<AOTTextureParameters>(binding, AOTEffectKind::TextureSource);
  if (parameters == nullptr || !parameters->isYUV) {
    return false;
  }
  switch (source) {
    case ChainBindingSource::YuvSubset: {
      // The node's record carries the source-space rect; the kernel clamps in normalized texture
      // coordinates, so the rect goes through the same sampler-aware normalization the plain leaf
      // records read.
      const auto* leaf = LeafBindingOf(ctx, binding.slot);
      if (leaf == nullptr) {
        return false;
      }
      const auto view =
          leaf->textureProxy != nullptr ? leaf->textureProxy->getTextureView() : nullptr;
      if (view == nullptr) {
        return false;
      }
      const SamplerState sampling(leaf->args.tileModeX, leaf->args.tileModeY, leaf->args.sampling);
      float rect[4] = {};
      LeafSubsetRectForSampler(leaf->textureProxy, view.get(), leaf->args.sampleArea, sampling,
                               rect);
      std::memcpy(out, rect, sizeof(rect));
      return true;
    }
    case ChainBindingSource::YuvAlphaStart: {
      // The RGBAAA alpha-plane offset rides in source pixels; the kernel adds it to a normalized
      // coordinate, so it resolves through the view here — the same read the plain leaf's
      // AlphaStart record makes.
      if (parameters->alphaStart == Point::Zero()) {
        return false;
      }
      const auto* yuvView = YUVViewOf(parameters->textureProxy);
      if (yuvView == nullptr) {
        return false;
      }
      const auto coord =
          yuvView->getTextureCoord(parameters->alphaStart.x, parameters->alphaStart.y);
      const float alphaStart[2] = {coord.x, coord.y};
      std::memcpy(out, alphaStart, sizeof(alphaStart));
      return true;
    }
    case ChainBindingSource::YuvHasRgbaaa:
      Store(out, parameters->hasRGBAAA ? 1 : 0);
      return true;
    case ChainBindingSource::YuvFormat: {
      int format = parameters->yuvFormat;
      if (format < 0) {
        const auto* yuvView = YUVViewOf(parameters->textureProxy);
        if (yuvView == nullptr) {
          return false;
        }
        if (yuvView->yuvFormat() == YUVFormat::I420) {
          format = 0;
        } else if (yuvView->yuvFormat() == YUVFormat::NV12) {
          format = 1;
        } else {
          return false;
        }
      }
      Store(out, format);
      return true;
    }
    case ChainBindingSource::YuvLimitedRange: {
      const auto* yuvView = YUVViewOf(parameters->textureProxy);
      if (yuvView == nullptr) {
        return false;
      }
      Store(out, IsLimitedYUVColorRange(yuvView->yuvColorSpace()) ? 1 : 0);
      return true;
    }
    case ChainBindingSource::YuvColorConversion: {
      const auto* yuvView = YUVViewOf(parameters->textureProxy);
      if (yuvView == nullptr) {
        return false;
      }
      const auto conversion = YUVColorConversionMatrix(yuvView->yuvColorSpace());
      if (!conversion.has_value()) {
        return false;
      }
      std::memcpy(out, conversion->data(), sizeof(float) * 12);
      return true;
    }
    default:
      return false;
  }
}

bool ReadBinding(const ReadContext& ctx, const ChainBinding& binding, uint8_t* out) {
  const auto& geometry = *ctx.geometry;
  switch (binding.source) {
    case ChainBindingSource::Unbound:
      return false;
    case ChainBindingSource::SlotColorMatrixLinear:
      return ReadSlotColorMatrix(binding, true, out);
    case ChainBindingSource::SlotColorMatrixTranslate:
      return ReadSlotColorMatrix(binding, false, out);
    case ChainBindingSource::SlotLumaCoefficients: {
      const auto* parameters = NodeParameters<ChainISA::LumaData>(binding, AOTEffectKind::Luma);
      if (parameters == nullptr) {
        return false;
      }
      const float coefficients[3] = {parameters->kr, parameters->kg, parameters->kb};
      std::memcpy(out, coefficients, sizeof(coefficients));
      return true;
    }
    case ChainBindingSource::SlotAlphaThreshold: {
      const auto* parameters =
          NodeParameters<ChainISA::AlphaThresholdData>(binding, AOTEffectKind::AlphaThreshold);
      if (parameters == nullptr) {
        return false;
      }
      Store(out, parameters->threshold);
      return true;
    }
    case ChainBindingSource::YuvSubset:
    case ChainBindingSource::YuvAlphaStart:
    case ChainBindingSource::YuvColorConversion:
    case ChainBindingSource::YuvLimitedRange:
    case ChainBindingSource::YuvHasRgbaaa:
    case ChainBindingSource::YuvFormat:
      return ReadYUV(ctx, binding, binding.source, out);
    case ChainBindingSource::SlotConstColor: {
      const auto* parameters =
          NodeParameters<AOTConstColorParameters>(binding, AOTEffectKind::ConstColor);
      if (parameters == nullptr) {
        return false;
      }
      std::memcpy(out, parameters->color.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotAARect: {
      const auto* parameters =
          NodeParameters<AOTRectCoverageParameters>(binding, AOTEffectKind::RectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      // The AA math evaluates to 0 at the rect coordinates, so the interpreter outsets by 0.5 to
      // interpolate from 0 at a half-pixel inset to 1 at a half-pixel outset. The pure fragment
      // takes the same input, so the assembled text must give it the same outset rect.
      const auto& rect = parameters->rect;
      const float outset[4] = {rect[0] - 0.5f, rect[1] - 0.5f, rect[2] + 0.5f, rect[3] + 0.5f};
      std::memcpy(out, outset, sizeof(outset));
      return true;
    }
    case ChainBindingSource::SlotLocalRect: {
      const auto* parameters =
          NodeParameters<AOTRectCoverageParameters>(binding, AOTEffectKind::RectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      std::memcpy(out, parameters->rect.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotLocalRectMatrix: {
      const auto* parameters =
          NodeParameters<AOTRectCoverageParameters>(binding, AOTEffectKind::RectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      StoreMatrix3(out, parameters->deviceToLocal);
      return true;
    }
    case ChainBindingSource::SlotRRect: {
      const auto* parameters =
          NodeParameters<AOTRRectCoverageParameters>(binding, AOTEffectKind::RRectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      std::memcpy(out, parameters->rect.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotRRectRadiiX: {
      const auto* parameters =
          NodeParameters<AOTRRectCoverageParameters>(binding, AOTEffectKind::RRectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      std::memcpy(out, parameters->radiiX.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotRRectRadiiY: {
      const auto* parameters =
          NodeParameters<AOTRRectCoverageParameters>(binding, AOTEffectKind::RRectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      std::memcpy(out, parameters->radiiY.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotRRectAntiAlias: {
      const auto* parameters =
          NodeParameters<AOTRRectCoverageParameters>(binding, AOTEffectKind::RRectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      Store(out, parameters->antiAlias);
      return true;
    }
    case ChainBindingSource::SlotRRectMatrix: {
      const auto* parameters =
          NodeParameters<AOTRRectCoverageParameters>(binding, AOTEffectKind::RRectCoverage);
      if (parameters == nullptr) {
        return false;
      }
      StoreMatrix3(out, parameters->deviceToLocal);
      return true;
    }
    case ChainBindingSource::GeometryColor: {
      const float color[4] = {geometry.commonColor.red, geometry.commonColor.green,
                              geometry.commonColor.blue, geometry.commonColor.alpha};
      std::memcpy(out, color, sizeof(color));
      return true;
    }
    case ChainBindingSource::GeometryHasCommonColor: {
      const int hasCommonColor = geometry.hasCommonColor ? 1 : 0;
      Store(out, hasCommonColor);
      return true;
    }
    case ChainBindingSource::GeometryStroke: {
      const int isStroke = geometry.isStroke ? 1 : 0;
      Store(out, isStroke);
      return true;
    }
    case ChainBindingSource::GeometryAA: {
      const int aaEnabled = geometry.aaType == AAType::None ? 0 : 1;
      Store(out, aaEnabled);
      return true;
    }
    case ChainBindingSource::GeometryCoverage:
      Store(out, geometry.coverage);
      return true;
    case ChainBindingSource::OutputSwizzle: {
      const int swizzle = ctx.info->getOutputSwizzle() == Swizzle::AAAA() ? 1 : 0;
      Store(out, swizzle);
      return true;
    }
    case ChainBindingSource::XpBlendMode: {
      const int blendMode = static_cast<int>(ctx.info->getBlendMode());
      LOGE("XPMODE upload=%d", blendMode);
      Store(out, blendMode);
      return true;
    }
    case ChainBindingSource::DstTextureUpperLeft: {
      const auto& xfer = ctx.info->getXferFacts();
      if (!xfer.dstTexture.has_value()) {
        return false;
      }
      const float upperLeft[2] = {xfer.dstTexture->offset.x, xfer.dstTexture->offset.y};
      std::memcpy(out, upperLeft, sizeof(upperLeft));
      return true;
    }
    case ChainBindingSource::DstTextureCoordScale: {
      const auto& xfer = ctx.info->getXferFacts();
      if (!xfer.dstTexture.has_value() || xfer.dstTexture->textureProxy == nullptr) {
        return false;
      }
      const auto view = xfer.dstTexture->textureProxy->getTextureView();
      if (view == nullptr || view->getTexture() == nullptr) {
        return false;
      }
      int width = view->width();
      int height = view->height();
      if (view->getTexture()->type() == TextureType::Rectangle) {
        width = 1;
        height = 1;
      }
      const float scales[2] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
      std::memcpy(out, scales, sizeof(scales));
      return true;
    }
    case ChainBindingSource::VertexMatrix: {
      const Matrix matrix =
          geometry.kind == GeometryKind::QuadPerEdgeAA ? Matrix::I() : geometry.viewMatrix;
      StoreMatrix3(out, matrix);
      return true;
    }
    case ChainBindingSource::VertexTexSubsetMatrix: {
      if (!geometry.hasSubset || !geometry.hasUVMatrix || ctx.transforms.empty()) {
        return false;
      }
      StoreMatrix3(out, ctx.transforms.front().getTotalMatrix());
      return true;
    }
    case ChainBindingSource::RTAdjust: {
      const auto adjust = GetRTAdjustArray(ctx.info->getRenderTarget());
      std::memcpy(out, adjust.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::CoordSourceMask:
      Store(out, static_cast<int>(ctx.program->coordSourceMask));
      return true;
    case ChainBindingSource::CoordTransform: {
      if (binding.index < 0 || static_cast<size_t>(binding.index) >= ctx.transforms.size()) {
        return false;
      }
      Matrix combined = {};
      combined.setConcat(ctx.transforms[static_cast<size_t>(binding.index)].getTotalMatrix(),
                         TransformBaseMatrix(ctx));
      StoreMatrix3(out, combined);
      return true;
    }
    case ChainBindingSource::GradientLayoutType:
    case ChainBindingSource::GradientColorizerKind:
    case ChainBindingSource::GradientLutLeaf:
    case ChainBindingSource::GradientLeftBorder:
    case ChainBindingSource::GradientRightBorder:
    case ChainBindingSource::GradientStart:
    case ChainBindingSource::GradientEnd:
    case ChainBindingSource::GradientScale01:
    case ChainBindingSource::GradientBias01:
    case ChainBindingSource::GradientScale23:
    case ChainBindingSource::GradientBias23:
    case ChainBindingSource::GradientThreshold:
    case ChainBindingSource::GradientBias:
    case ChainBindingSource::GradientScale:
    case ChainBindingSource::GradientThresholds1_7:
    case ChainBindingSource::GradientThresholds9_13:
    case ChainBindingSource::GradientScales:
    case ChainBindingSource::GradientBiases:
      return ReadGradient(ctx, binding, binding.source, binding.index, out);
    case ChainBindingSource::NoiseFrequencyStitch: {
      const auto* noise =
          NodeParameters<AOTPerlinNoiseParameters>(binding, AOTEffectKind::PerlinNoiseSource);
      if (noise == nullptr) {
        return false;
      }
      const float frequencyStitch[4] = {noise->baseFrequencyX, noise->baseFrequencyY,
                                        static_cast<float>(noise->stitchWidth),
                                        static_cast<float>(noise->stitchHeight)};
      std::memcpy(out, frequencyStitch, sizeof(frequencyStitch));
      return true;
    }
    case ChainBindingSource::NoiseModeOctaves: {
      const auto* noise =
          NodeParameters<AOTPerlinNoiseParameters>(binding, AOTEffectKind::PerlinNoiseSource);
      if (noise == nullptr) {
        return false;
      }
      const int modeOctaves[4] = {noise->noiseType, noise->numOctaves, noise->stitchTiles ? 1 : 0,
                                  ctx.program->noiseSamplerBase};
      std::memcpy(out, modeOctaves, sizeof(modeOctaves));
      return true;
    }
    case ChainBindingSource::ChainFlags:
    case ChainBindingSource::ChainSrcTF0:
    case ChainBindingSource::ChainSrcTF1:
    case ChainBindingSource::ChainSrcTFType:
    case ChainBindingSource::ChainSrcOOTF:
    case ChainBindingSource::ChainColorXform:
    case ChainBindingSource::ChainDstOOTF:
    case ChainBindingSource::ChainDstTF0:
    case ChainBindingSource::ChainDstTF1:
    case ChainBindingSource::ChainDstTFType:
      return ReadColorSpace(binding, binding.source, out);
    case ChainBindingSource::LeafSubset:
    case ChainBindingSource::LeafSubsetClampOn:
    case ChainBindingSource::LeafAlphaStart:
    case ChainBindingSource::LeafHasRgbaaa:
    case ChainBindingSource::TiledShaderModeX:
    case ChainBindingSource::TiledShaderModeY:
    case ChainBindingSource::TiledSubset:
    case ChainBindingSource::TiledClamp:
    case ChainBindingSource::TiledDimension:
    case ChainBindingSource::BlurStep: {
      if (binding.node == nullptr || binding.node->kind != AOTEffectKind::OpaqueKernel) {
        return false;
      }
      const auto* parameters = std::get_if<AOTOpaqueKernelParameters>(&binding.node->parameters);
      if (parameters == nullptr) {
        return false;
      }
      const float step[2] = {parameters->stepLength * (parameters->vertical ? 0.0f : 1.0f),
                             parameters->stepLength * (parameters->vertical ? 1.0f : 0.0f)};
      std::memcpy(out, step, sizeof(step));
      return true;
    }
    case ChainBindingSource::BlurRadius: {
      if (binding.node == nullptr || binding.node->kind != AOTEffectKind::OpaqueKernel) {
        return false;
      }
      const auto* parameters = std::get_if<AOTOpaqueKernelParameters>(&binding.node->parameters);
      if (parameters == nullptr) {
        return false;
      }
      Store(out, parameters->kernelRadius);
      return true;
    }
    case ChainBindingSource::BlurKernel: {
      if (binding.node == nullptr || binding.node->kind != AOTEffectKind::OpaqueKernel) {
        return false;
      }
      const auto* parameters = std::get_if<AOTOpaqueKernelParameters>(&binding.node->parameters);
      if (parameters == nullptr) {
        return false;
      }
      std::memcpy(out, parameters->packedKernel.data(), sizeof(float) * 68);
      return true;
    }
    case ChainBindingSource::BlurTiledChild: {
      const auto* childBinding = LeafBindingOf(ctx, binding.index);
      Store(out, childBinding != nullptr && childBinding->tiledRecipe.has_value() ? 1 : 0);
      return true;
    }
    case ChainBindingSource::TiledStrict:
      return ReadLeafRecord(ctx, binding.index, binding.source, out);
    case ChainBindingSource::LeafReadSwizzles: {
      // The plain-fetch leaves read through their view's swizzle mode; the leaf slots the body
      // samples are exactly the plan's leaf varyings (a multi-plane source owns its three samplers
      // but reads them through the YUV operator, not the plain fetch).
      int readSwizzles[4] = {};
      for (size_t index = 0; index < ctx.program->bindings.size() && index < 4; ++index) {
        if (ctx.table->leafVaryings.count(static_cast<int>(index)) == 0 ||
            ctx.program->bindings[index].kind != AOTBinding::Kind::Leaf) {
          continue;
        }
        const auto view = ctx.program->bindings[index].textureProxy != nullptr
                              ? ctx.program->bindings[index].textureProxy->getTextureView()
                              : nullptr;
        if (view != nullptr) {
          readSwizzles[index] = AOTReadSwizzleMode(view->getTexture()->format());
        }
      }
      std::memcpy(out, readSwizzles, sizeof(readSwizzles));
      return true;
    }
    case ChainBindingSource::DeviceCoordMatrix: {
      const auto* binding2 = MaskBindingOf(ctx);
      if (binding2 == nullptr || binding2->textureProxy == nullptr) {
        return false;
      }
      const auto view = binding2->textureProxy->getTextureView();
      if (view == nullptr) {
        return false;
      }
      StoreMatrix3(out, DeviceMaskCoordMatrix(binding2->uvMatrix, *view));
      return true;
    }
    case ChainBindingSource::HasMaskTexture: {
      const int hasMaskTexture = ctx.program->hasMaskTexture ? 1 : 0;
      Store(out, hasMaskTexture);
      return true;
    }
    case ChainBindingSource::DeviceMaskSubset: {
      const auto* binding2 = MaskBindingOf(ctx);
      if (binding2 == nullptr || binding2->textureProxy == nullptr) {
        return false;
      }
      const auto view = binding2->textureProxy->getTextureView();
      if (view == nullptr) {
        return false;
      }
      float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
      if (view->getTexture()->type() == TextureType::Rectangle) {
        rect[2] = static_cast<float>(view->width());
        rect[3] = static_cast<float>(view->height());
      }
      std::memcpy(out, rect, sizeof(rect));
      return true;
    }
    case ChainBindingSource::DeviceLeafMask: {
      uint32_t deviceMask = 0;
      for (const auto& deviceLeaf : ctx.program->deviceLeaves) {
        deviceMask |= 1u << static_cast<uint32_t>(deviceLeaf.leafIndex);
      }
      const int mask = static_cast<int>(deviceMask);
      Store(out, mask);
      return true;
    }
    case ChainBindingSource::DeviceLeafCoordMatrix: {
      for (const auto& deviceLeaf : ctx.program->deviceLeaves) {
        if (deviceLeaf.leafIndex != binding.index) {
          continue;
        }
        const auto view = deviceLeaf.textureProxy != nullptr
                              ? deviceLeaf.textureProxy->getTextureView()
                              : nullptr;
        if (view == nullptr) {
          return false;
        }
        StoreMatrix3(out, DeviceMaskCoordMatrix(deviceLeaf.uvMatrix, *view));
        return true;
      }
      // A plain leaf never samples through this matrix; the identity keeps the element defined.
      StoreMatrix3(out, Matrix::I());
      return true;
    }
  }
  return false;
}

// One source sampler binding: the proxy whose view is bound and the sampler state the draw asks
// for.
struct ResolvedSampler {
  std::shared_ptr<TextureProxy> proxy = nullptr;
  SamplerState state = {};
};

const AOTBinding* BindingAt(const AOTProgram& program, int index) {
  if (index < 0 || static_cast<size_t>(index) >= program.bindings.size()) {
    return nullptr;
  }
  return &program.bindings[static_cast<size_t>(index)];
}

bool ResolveSampler(const ChainSamplerBinding& sampler, const AOTProgram& program,
                    const ProgramInfo& info, ResolvedSampler* out) {
  const AOTBinding* binding = nullptr;
  switch (sampler.source) {
    case ChainSamplerSource::Leaf:
      binding = BindingAt(program, sampler.index);
      break;
    case ChainSamplerSource::Lut:
      binding = BindingAt(program, program.lutLeafIndex);
      break;
    case ChainSamplerSource::NoiseLookup:
      binding = program.noiseSamplerBase >= 0
                    ? BindingAt(program, program.noiseSamplerBase + sampler.index)
                    : nullptr;
      break;
    case ChainSamplerSource::Mask: {
      for (const auto& candidate : program.bindings) {
        if (candidate.kind == AOTBinding::Kind::Mask) {
          binding = &candidate;
          break;
        }
      }
      break;
    }
    case ChainSamplerSource::DstTexture:
      out->proxy = info.getXferFacts().dstProxy();
      return out->proxy != nullptr;
  }
  if (binding == nullptr || binding->textureProxy == nullptr) {
    return false;
  }
  out->proxy = binding->textureProxy;
  out->state =
      SamplerState(binding->args.tileModeX, binding->args.tileModeY, binding->args.sampling);
  return true;
}

}  // namespace

ChainBindingWriter::ChainBindingWriter(Variant variant, ChainBindings bindings)
    : ProgramWriter(std::move(variant)), bindings(std::move(bindings)) {
}

void ChainBindingWriter::writeUniforms(const ProgramInfo& info, UniformData* vertexUniformData,
                                       UniformData* fragmentUniformData) const {
  const auto* sources = info.getSources();
  const AOTProgram* program = sources != nullptr ? sources->chain : nullptr;
  auto* ir = info.getCarriedIR();
  if (program == nullptr || ir == nullptr) {
    LOGE("ChainBindingWriter: the draw carries no compiled chain program; uniforms stay unwritten");
    return;
  }
  ReadContext ctx = {};
  ctx.info = &info;
  ctx.program = program;
  ctx.geometry = &info.getGeometryLayout().parameters;
  ctx.table = &bindings;
  ctx.transforms = BuildChainCoordTransforms(*ir, *program);
  auto* vertexBytes = vertexUniformData != nullptr ? vertexUniformData->writableBytes() : nullptr;
  if (vertexBytes != nullptr) {
    const size_t size = vertexUniformData->size();
    for (const auto& binding : bindings.vertex) {
      if (static_cast<size_t>(binding.offset) + binding.size > size) {
        continue;
      }
      ReadBinding(ctx, binding, vertexBytes + binding.offset);
    }
  }
  auto* fragmentBytes =
      fragmentUniformData != nullptr ? fragmentUniformData->writableBytes() : nullptr;

  if (fragmentBytes != nullptr) {
    const size_t size = fragmentUniformData->size();
    for (const auto& binding : bindings.fragment) {
      if (static_cast<size_t>(binding.offset) + binding.size > size) {
        continue;
      }
      ReadBinding(ctx, binding, fragmentBytes + binding.offset);
    }
  }
}

bool ChainBindingWriter::writeSamplers(const ProgramInfo& info, Program* program,
                                       RenderPass* renderPass) const {
  const auto* sources = info.getSources();
  if (sources == nullptr || sources->chain == nullptr) {
    LOGE("ChainBindingWriter: the draw carries no compiled chain program; nothing binds");
    return false;
  }
  const auto& chain = *sources->chain;
  if (program->expectedTextureCount() != bindings.samplers.size()) {
    LOGE(
        "ChainBindingWriter: the program declares %u sampler slots but the emit declared %zu; the "
        "declaration and the draw's sources disagree",
        program->expectedTextureCount(), bindings.samplers.size());
    return false;
  }
  auto* drawContext = info.getRenderTarget()->getContext();
  auto gpu = drawContext->gpu();
  auto* cache = drawContext->globalCache();
  for (size_t index = 0; index < bindings.samplers.size(); ++index) {
    ResolvedSampler resolved = {};
    ResolveSampler(bindings.samplers[index], chain, info, &resolved);
    auto view = resolved.proxy != nullptr ? resolved.proxy->getTextureView() : nullptr;
    if (view == nullptr) {
      renderPass->setTexture(program->getTextureBinding(static_cast<unsigned>(index)),
                             cache->getOrCreateDummyTexture(), cache->getOrCreateDummySampler());
      continue;
    }
    // The multi-plane source binds one texture per plane: the leaf index the sampler carries
    // addresses the YUV view's plane textures (Y, U-or-UV, V). An interleaved layout declares the
    // third sampler but never samples it, so a plane past the view's texture count keeps the
    // dummy binding below.
    std::shared_ptr<Texture> texture;
    if (view->isYUV()) {
      const auto* yuvView = static_cast<const YUVTextureView*>(view.get());
      const auto plane = static_cast<size_t>(bindings.samplers[index].index);
      if (plane < yuvView->textureCount()) {
        texture = yuvView->getTextureAt(plane);
      }
    } else {
      texture = view->getTexture();
    }
    if (texture == nullptr) {
      renderPass->setTexture(program->getTextureBinding(static_cast<unsigned>(index)),
                             cache->getOrCreateDummyTexture(), cache->getOrCreateDummySampler());
      continue;
    }
    const auto& state = resolved.state;
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    renderPass->setTexture(program->getTextureBinding(static_cast<unsigned>(index)), texture,
                           gpu->createSampler(descriptor));
  }
  // The modules statically reference every sampler the variant declares (the leaf fetch's index
  // dispatch), so the slots the composition leaves unused still need a valid binding at draw.
  for (unsigned index = static_cast<unsigned>(bindings.samplers.size());
       index < program->textureSlotCount(); ++index) {
    renderPass->setTexture(program->getTextureBinding(index), cache->getOrCreateDummyTexture(),
                           cache->getOrCreateDummySampler());
  }
  return true;
}

}  // namespace tgfx
