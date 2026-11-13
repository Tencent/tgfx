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
//  license is distributed on an "as is" basis, without warranties of any kind, either express or
//  implied. see the license for the specific language governing permissions and limitations under
//  the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/ProgramKey.h"
#include <cstring>
#include <type_traits>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/spec/SpecValues.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

static void WriteBytes(BytesKey* key, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t offset = 0; offset < size; offset += 4) {
    if (offset + 4 <= size) {
      key->write(bytes + offset);
    } else {
      uint8_t word[4] = {};
      std::memcpy(word, bytes + offset, size - offset);
      key->write(word);
    }
  }
}

static void HashSamplerState(const SamplerState& state, BytesKey* key) {
  key->write(static_cast<uint32_t>(state.tileModeX));
  key->write(static_cast<uint32_t>(state.tileModeY));
  key->write(static_cast<uint32_t>(state.minFilterMode));
  key->write(static_cast<uint32_t>(state.magFilterMode));
  key->write(static_cast<uint32_t>(state.mipmapMode));
}

static void HashTextureParameters(const AOTTextureParameters& params, BytesKey* key) {
  key->write(static_cast<uint32_t>(params.samplingKind));
  HashSamplerState(params.samplerState, key);
  key->write(static_cast<uint32_t>(params.constraint));
  key->write(static_cast<uint32_t>(params.subset.has_value() ? 1u : 0u));
  if (params.subset.has_value()) {
    key->write(params.subset->left);
    key->write(params.subset->top);
    key->write(params.subset->right);
    key->write(params.subset->bottom);
  }
  key->write(params.alphaStart.x);
  key->write(params.alphaStart.y);
  key->write(static_cast<uint32_t>(params.isYUV ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.isAlphaOnly ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.hasRGBAAA ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.hasPerspective ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.isIntermediate ? 1u : 0u));
  key->write(static_cast<uint32_t>(params.yuvFormat));
  key->write(static_cast<uint32_t>(params.uvMatrix.hasPerspective() ? 1u : 0u));
  auto view = params.textureProxy != nullptr ? params.textureProxy->getTextureView() : nullptr;
  auto texture = view != nullptr ? view->getTexture() : nullptr;
  if (texture != nullptr) {
    TextureView::ComputeTextureKey(texture, key);
  } else {
    key->write(static_cast<const void*>(params.textureProxy.get()));
  }
}

static void HashEffectParameters(const AOTEffectParameters& parameters, BytesKey* key) {
  std::visit(
      [key](const auto& params) {
        using T = std::decay_t<decltype(params)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return;
        } else if constexpr (std::is_same_v<T, AOTTextureParameters>) {
          HashTextureParameters(params, key);
        } else if constexpr (std::is_same_v<T, AOTColorSpaceXformParameters>) {
          key->write(static_cast<const void*>(params.steps.get()));
        } else if constexpr (std::is_same_v<T, AOTPerlinNoiseParameters>) {
          key->write(static_cast<uint32_t>(params.noiseType));
          key->write(static_cast<uint32_t>(params.numOctaves));
          key->write(static_cast<uint32_t>(params.stitchTiles ? 1u : 0u));
          key->write(static_cast<uint32_t>(params.stitchWidth));
          key->write(static_cast<uint32_t>(params.stitchHeight));
          key->write(params.baseFrequencyX);
          key->write(params.baseFrequencyY);
          key->write(static_cast<uint32_t>(params.uvMatrix.hasPerspective() ? 1u : 0u));
          key->write(static_cast<const void*>(params.lookupData.get()));
        } else if constexpr (std::is_same_v<T, AOTGradientParameters>) {
          key->write(static_cast<uint32_t>(params.layoutType));
          key->write(static_cast<uint32_t>(params.colorizerKind));
          key->write(static_cast<uint32_t>(params.hasPerspective ? 1u : 0u));
          key->write(static_cast<const void*>(params.lutProxy.get()));
          key->write(params.coordMatrix.hasPerspective() ? 1.0f : 0.0f);
        } else if constexpr (std::is_same_v<T, AOTOpaqueKernelParameters>) {
          key->write(static_cast<uint32_t>(params.kernel));
          key->write(static_cast<uint32_t>(params.sigma));
          key->write(static_cast<uint32_t>(params.maxRadius));
          key->write(static_cast<uint32_t>(params.geometryKind.has_value() ? 1u : 0u));
          if (params.geometryKind.has_value()) {
            key->write(static_cast<uint32_t>(*params.geometryKind));
          }
          key->write(static_cast<uint32_t>(params.dispersionOn ? 1u : 0u));
          key->write(static_cast<uint32_t>(params.hasPerspective ? 1u : 0u));
          key->write(static_cast<uint32_t>(params.tentField));
          key->write(static_cast<const void*>(params.glassSourceProxy.get()));
          key->write(static_cast<const void*>(params.glassShape.maskProxy.get()));
          key->write(static_cast<const void*>(params.glassShape.edgeMaskProxy.get()));
        } else if constexpr (std::is_trivially_copyable_v<T>) {
          WriteBytes(key, &params, sizeof(T));
        }
      },
      parameters);
}

static void HashGraph(const AOTEffectGraph& graph, BytesKey* key) {
  key->write(static_cast<uint32_t>(graph.nodeCount()));
  for (uint32_t index = 0; index < graph.nodeCount(); ++index) {
    auto* node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      key->write(static_cast<uint32_t>(0xFFFFFFFFu));
      continue;
    }
    key->write(static_cast<uint32_t>(node->kind));
    HashEffectParameters(node->parameters, key);
  }
}

void HashDrawIR(const DrawIR& ir, BytesKey* key) {
  HashGraph(ir.color, key);
  key->write(static_cast<uint32_t>(ir.colorFPCount));
  key->write(static_cast<uint32_t>(ir.coverageFPCount));
  key->write(static_cast<uint32_t>(ir.coverage.size()));
  for (const auto& term : ir.coverage) {
    key->write(static_cast<uint32_t>(term.kind));
    HashGraph(term.graph, key);
    HashTextureParameters(term.mask, key);
  }
}

BytesKey MakeProgramKey(Context* context, const ProgramInfo& info, uint32_t variantId,
                        const SpecValues& spec) {
  BytesKey key = {};
  key.write(variantId);
  // The values carry no length of their own, so a longer specialization could shift the rest of
  // the key into the words of a shorter one and collide.
  key.write(static_cast<uint32_t>(spec.values.size()));
  spec.writeKey(&key);
  (void)context;
  // The geometry's key contribution: the kind (the class ID's replacement), the perspective flag,
  // and every live attribute's format.
  const auto& geometryLayout = info.getGeometryLayout();
  key.write(static_cast<uint32_t>(geometryLayout.parameters.kind));
  key.write(static_cast<uint32_t>(geometryLayout.parameters.hasUVPerspective ? 1u : 0u));
  for (const auto& attribute : geometryLayout.vertexAttributes) {
    if (!attribute.empty()) {
      key.write(static_cast<uint32_t>(attribute.format()));
    }
  }
  for (const auto& attribute : geometryLayout.instanceAttributes) {
    if (!attribute.empty()) {
      key.write(static_cast<uint32_t>(attribute.format()));
    }
  }
  // The output formula's key contribution: whether the shader runs the xfer stage, and the
  // destination texture when the formula samples one. blendMode already rides the pipeline state
  // key below, and a null proxy means the backend's framebuffer fetch (the third state the old
  // processor's class ID used to encode).
  const auto& xfer = info.getXferFacts();
  key.write(static_cast<uint32_t>(xfer.shaderBlend() ? 1u : 0u));
  if (xfer.shaderBlend()) {
    const auto& dstProxy = xfer.dstTexture->textureProxy;
    auto dstView = dstProxy != nullptr ? dstProxy->getTextureView() : nullptr;
    if (dstView != nullptr) {
      TextureView::ComputeTextureKey(dstView->getTexture(), &key);
    }
  }
  // The blend formula reads this bit (see ProgramInfo::getPipelineColorAttachment), so it is
  // pipeline state even though it rides the processor list.
  key.write(static_cast<uint32_t>(info.blendFormulaHasCoverage() ? 1 : 0));
  info.writePipelineStateKey(&key);
  return key;
}

}  // namespace tgfx
