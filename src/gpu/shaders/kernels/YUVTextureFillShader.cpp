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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/YUVTextureFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

std::optional<YUVTextureFillShader::Inputs> YUVTextureFillShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<YUVTextureFillShader::Inputs> YUVTextureFillShader::ExtractFacts(
    const AOTDrawFacts& facts, PermutationMatchFailure*) {
  if (facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  // The precompiled vert has no texSubset attribute, so subset-carrying quads fall back.
  if (facts.geometry.kind != AOTGeometryKind::QuadPerEdgeAA || facts.geometry.hasSubset) {
    return std::nullopt;
  }
  // The color expression reads off the carried IR: the multi-plane source leaf with the bounded
  // color-grade operators above it (the node IS the contract; no processor-tree walk — the plain
  // bare-source route and the planned color-grade route produce the same graph, so the old
  // dual-form match is gone).
  if (facts.ir == nullptr) {
    return std::nullopt;
  }
  AOTChainBuilder::AOTYUVTailShape shape = {};
  if (!AOTChainBuilder::CollectYUVTail(facts.ir->color, &shape)) {
    return std::nullopt;
  }
  // No coverage processors (the runtime route stays authoritative for clipped video), and the
  // color processors are exactly the source plus the tail the IR carries — the staleness guard
  // keeps the tree and the graph in agreement.
  if (facts.colorFPCount < 1 || facts.colorFPCount > 1 + shape.ops.size() ||
      facts.colorFPCount != facts.fragmentCount) {
    return std::nullopt;
  }
  int yuvFormat = shape.leaf->yuvFormat;
  if (yuvFormat < 0) {
    // A pending multi-plane upload lowers with the layout unknown (it exists only once the
    // view does): read it off the tree's TextureEffect — an instance-time fact, like the
    // upload values that stay on the processor until the node contract lands. A tree that
    // disagrees with the leaf's YUV-ness is a stale record-time lowering: reject.
    const TextureEffect* texture = nullptr;
    for (size_t index = 0; index < facts.fragmentCount; ++index) {
      auto* fp = facts.info->getFragmentProcessor(index);
      if (IsProcessor<TextureEffect>(fp)) {
        texture = static_cast<const TextureEffect*>(fp);
        break;
      }
    }
    if (texture == nullptr || !texture->isYUV()) {
      return std::nullopt;
    }
    if (texture->yuvFormat() == YUVFormat::I420) {
      yuvFormat = 0;
    } else if (texture->yuvFormat() == YUVFormat::NV12) {
      yuvFormat = 1;
    } else {
      return std::nullopt;
    }
  }
  Inputs inputs;
  inputs.hasUVMatrix = facts.geometry.hasUVMatrix;
  inputs.yuvFormat = yuvFormat;
  switch (facts.xfer) {
    case AOTXferKind::None:
      inputs.xpType = XP_NONE;
      break;
    case AOTXferKind::DstTexture:
      inputs.xpType = XP_DST_TEXTURE;
      break;
    case AOTXferKind::FrameBufferFetch:
      inputs.xpType = XP_FRAMEBUFFER_FETCH;
      break;
    case AOTXferKind::Unsupported:
      inputs.xpType = XP_UNSUPPORTED;
      break;
  }
  return inputs;
}

}  // namespace tgfx
