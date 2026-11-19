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
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/ir/Program.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
namespace {

// Everything the readers address: the draw's selection, the compiled program, the geometry facts
// and the coordinate transforms the stored vertex stage consumes.
struct ReadContext {
  const ProgramInfo* info = nullptr;
  const AOTProgram* program = nullptr;
  const AOTGeometryParameters* geometry = nullptr;
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

// The row-major 9-element matrix the coverage slots carry, in the same column-major spelling.
void StoreMatrix3(uint8_t* out, const std::array<float, 9>& matrix) {
  const float data[12] = {matrix[0], matrix[3], matrix[6], 0.0f,      matrix[1], matrix[4],
                          matrix[7], 0.0f,      matrix[2], matrix[5], matrix[8], 0.0f};
  std::memcpy(out, data, sizeof(data));
}

const AOTChainSlot* SlotOf(const ReadContext& ctx, int slot) {
  if (slot < 0 || static_cast<size_t>(slot) >= ctx.program->slots.size()) {
    return nullptr;
  }
  return &ctx.program->slots[static_cast<size_t>(slot)];
}

const AOTChainSlot* SlotWithOp(const ReadContext& ctx, AOTChainOp op) {
  for (const auto& slot : ctx.program->slots) {
    if (slot.op == op) {
      return &slot;
    }
  }
  return nullptr;
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

bool ReadSlotColorMatrix(const ReadContext& ctx, int slot, bool linear, uint8_t* out) {
  const auto* chainSlot = SlotOf(ctx, slot);
  if (chainSlot == nullptr) {
    return false;
  }
  float linearData[16] = {};
  float translate[4] = {};
  PackAOTColorMatrix(chainSlot->colorMatrix.matrix, linearData, translate);
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

bool ReadGradient(const ReadContext& ctx, ChainBindingSource source, int index, uint8_t* out) {
  const auto* chainSlot = SlotWithOp(ctx, AOTChainOp::Gradient);
  if (chainSlot == nullptr) {
    return false;
  }
  const auto& gradient = chainSlot->gradient;
  switch (source) {
    case ChainBindingSource::GradientLayoutType:
      Store(out, gradient.layoutType);
      return true;
    case ChainBindingSource::GradientColorizerKind:
      Store(out, gradient.colorizerKind);
      return true;
    case ChainBindingSource::GradientLutLeaf:
      Store(out, ctx.program->lutLeafIndex);
      return true;
    case ChainBindingSource::GradientLeftBorder:
      std::memcpy(out, gradient.leftBorder.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientRightBorder:
      std::memcpy(out, gradient.rightBorder.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientStart:
      std::memcpy(out, gradient.start.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientEnd:
      std::memcpy(out, gradient.end.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientScale01:
      std::memcpy(out, gradient.scale01.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientBias01:
      std::memcpy(out, gradient.bias01.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientScale23:
      std::memcpy(out, gradient.scale23.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientBias23:
      std::memcpy(out, gradient.bias23.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientThreshold:
      Store(out, gradient.threshold);
      return true;
    case ChainBindingSource::GradientBias:
      Store(out, gradient.bias);
      return true;
    case ChainBindingSource::GradientScale:
      Store(out, gradient.scale);
      return true;
    case ChainBindingSource::GradientThresholds1_7:
      std::memcpy(out, gradient.thresholds1_7.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientThresholds9_13:
      std::memcpy(out, gradient.thresholds9_13.data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientScales:
      if (index < 0 || index >= 8) {
        return false;
      }
      std::memcpy(out, gradient.scales[static_cast<size_t>(index)].data(), sizeof(float) * 4);
      return true;
    case ChainBindingSource::GradientBiases:
      if (index < 0 || index >= 8) {
        return false;
      }
      std::memcpy(out, gradient.biases[static_cast<size_t>(index)].data(), sizeof(float) * 4);
      return true;
    default:
      return false;
  }
}

bool ReadColorSpace(const ReadContext& ctx, ChainBindingSource source, uint8_t* out) {
  const auto* chainSlot = SlotWithOp(ctx, AOTChainOp::ColorSpaceXform);
  if (chainSlot == nullptr || chainSlot->colorSpaceXform.steps == nullptr) {
    return false;
  }
  ColorSpaceXformValues values = {};
  ResolveColorSpaceXformValues(chainSlot->colorSpaceXform.steps.get(), &values);
  switch (source) {
    case ChainBindingSource::CSFlags:
      Store(out, values.flags);
      return true;
    case ChainBindingSource::CSTransferSrc0:
      std::memcpy(out, values.srcTF0, sizeof(values.srcTF0));
      return true;
    case ChainBindingSource::CSTransferSrc1:
      std::memcpy(out, values.srcTF1, sizeof(values.srcTF1));
      return true;
    case ChainBindingSource::CSTransferSrcType:
      Store(out, values.srcTFType);
      return true;
    case ChainBindingSource::CSOotfSrc:
      std::memcpy(out, values.srcOOTF, sizeof(values.srcOOTF));
      return true;
    case ChainBindingSource::CSColorXform:
      std::memcpy(out, values.colorXform, sizeof(values.colorXform));
      return true;
    case ChainBindingSource::CSOotfDst:
      std::memcpy(out, values.dstOOTF, sizeof(values.dstOOTF));
      return true;
    case ChainBindingSource::CSTransferDst0:
      std::memcpy(out, values.dstTF0, sizeof(values.dstTF0));
      return true;
    case ChainBindingSource::CSTransferDst1:
      std::memcpy(out, values.dstTF1, sizeof(values.dstTF1));
      return true;
    case ChainBindingSource::CSTransferDstType:
      Store(out, values.dstTFType);
      return true;
    default:
      return false;
  }
}

bool ReadBinding(const ReadContext& ctx, const ChainBinding& binding, uint8_t* out) {
  const auto& geometry = *ctx.geometry;
  switch (binding.source) {
    case ChainBindingSource::SlotColorMatrixLinear:
      return ReadSlotColorMatrix(ctx, binding.slot, true, out);
    case ChainBindingSource::SlotColorMatrixTranslate:
      return ReadSlotColorMatrix(ctx, binding.slot, false, out);
    case ChainBindingSource::SlotLumaCoefficients: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      const float coefficients[3] = {chainSlot->luma.kr, chainSlot->luma.kg, chainSlot->luma.kb};
      std::memcpy(out, coefficients, sizeof(coefficients));
      return true;
    }
    case ChainBindingSource::SlotAlphaThreshold: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      Store(out, chainSlot->alphaThreshold.threshold);
      return true;
    }
    case ChainBindingSource::SlotConstColor: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      std::memcpy(out, chainSlot->constColor.color.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotAARect: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      // The AA math evaluates to 0 at the rect coordinates, so the interpreter outsets by 0.5 to
      // interpolate from 0 at a half-pixel inset to 1 at a half-pixel outset. The pure fragment
      // takes the same input, so the assembled text must give it the same outset rect.
      const auto& rect = chainSlot->rectCoverage.rect;
      const float outset[4] = {rect[0] - 0.5f, rect[1] - 0.5f, rect[2] + 0.5f, rect[3] + 0.5f};
      std::memcpy(out, outset, sizeof(outset));
      return true;
    }
    case ChainBindingSource::SlotLocalRect: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      std::memcpy(out, chainSlot->localRectCoverage.rect.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotLocalRectMatrix: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      StoreMatrix3(out, chainSlot->localRectCoverage.deviceToLocal);
      return true;
    }
    case ChainBindingSource::SlotRRect: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      std::memcpy(out, chainSlot->rrectCoverage.rect.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotRRectRadiiX: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      std::memcpy(out, chainSlot->rrectCoverage.radiiX.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotRRectRadiiY: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      std::memcpy(out, chainSlot->rrectCoverage.radiiY.data(), sizeof(float) * 4);
      return true;
    }
    case ChainBindingSource::SlotRRectAntiAlias: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      Store(out, chainSlot->rrectCoverage.antiAlias);
      return true;
    }
    case ChainBindingSource::SlotRRectMatrix: {
      const auto* chainSlot = SlotOf(ctx, binding.slot);
      if (chainSlot == nullptr) {
        return false;
      }
      StoreMatrix3(out, chainSlot->rrectCoverage.deviceToLocal);
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
      return ReadGradient(ctx, binding.source, binding.index, out);
    case ChainBindingSource::NoiseFrequencyStitch: {
      const auto* chainSlot = SlotWithOp(ctx, AOTChainOp::PerlinNoise);
      if (chainSlot == nullptr) {
        return false;
      }
      const auto& noise = chainSlot->noise;
      const float frequencyStitch[4] = {noise.baseFrequencyX, noise.baseFrequencyY,
                                        static_cast<float>(noise.stitchWidth),
                                        static_cast<float>(noise.stitchHeight)};
      std::memcpy(out, frequencyStitch, sizeof(frequencyStitch));
      return true;
    }
    case ChainBindingSource::NoiseModeOctaves: {
      const auto* chainSlot = SlotWithOp(ctx, AOTChainOp::PerlinNoise);
      if (chainSlot == nullptr) {
        return false;
      }
      const auto& noise = chainSlot->noise;
      const int modeOctaves[4] = {noise.noiseType, noise.numOctaves, noise.stitchTiles ? 1 : 0,
                                  ctx.program->noiseSamplerBase};
      std::memcpy(out, modeOctaves, sizeof(modeOctaves));
      return true;
    }
    case ChainBindingSource::CSFlags:
    case ChainBindingSource::CSTransferSrc0:
    case ChainBindingSource::CSTransferSrc1:
    case ChainBindingSource::CSTransferSrcType:
    case ChainBindingSource::CSOotfSrc:
    case ChainBindingSource::CSColorXform:
    case ChainBindingSource::CSOotfDst:
    case ChainBindingSource::CSTransferDst0:
    case ChainBindingSource::CSTransferDst1:
    case ChainBindingSource::CSTransferDstType:
      return ReadColorSpace(ctx, binding.source, out);
    case ChainBindingSource::LeafSubset:
    case ChainBindingSource::LeafSubsetClampOn:
    case ChainBindingSource::LeafAlphaStart:
    case ChainBindingSource::LeafHasRgbaaa:
      return ReadLeafRecord(ctx, binding.index, binding.source, out);
    case ChainBindingSource::LeafReadSwizzles: {
      int readSwizzles[4] = {};
      for (size_t index = 0; index < ctx.program->bindings.size() && index < 4; ++index) {
        if (index >= ctx.program->slots.size() ||
            ctx.program->slots[index].op != AOTChainOp::Texture ||
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
      const auto* binding = MaskBindingOf(ctx);
      if (binding == nullptr || binding->textureProxy == nullptr) {
        return false;
      }
      const auto view = binding->textureProxy->getTextureView();
      if (view == nullptr) {
        return false;
      }
      StoreMatrix3(out, DeviceMaskCoordMatrix(binding->uvMatrix, *view));
      return true;
    }
    case ChainBindingSource::HasMaskTexture: {
      const int hasMaskTexture = ctx.program->hasMaskTexture ? 1 : 0;
      Store(out, hasMaskTexture);
      return true;
    }
    case ChainBindingSource::DeviceMaskSubset: {
      const auto* binding = MaskBindingOf(ctx);
      if (binding == nullptr || binding->textureProxy == nullptr) {
        return false;
      }
      const auto view = binding->textureProxy->getTextureView();
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
  if (program == nullptr) {
    LOGE("ChainBindingWriter: the draw carries no compiled chain program; uniforms stay unwritten");
    return;
  }
  ReadContext ctx = {};
  ctx.info = &info;
  ctx.program = program;
  ctx.geometry = &info.getGeometryLayout().parameters;
  ctx.transforms = BuildChainCoordTransforms(*program);
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
    const auto& state = resolved.state;
    SamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                 state.minFilterMode, state.magFilterMode, state.mipmapMode);
    renderPass->setTexture(program->getTextureBinding(static_cast<unsigned>(index)),
                           view->getTexture(), gpu->createSampler(descriptor));
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
