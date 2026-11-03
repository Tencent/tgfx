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
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/YUVTextureFillShader.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/plan/AOTChainBuilder.h"
#include "gpu/aot/select/DrawShape.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/YUVTextureView.h"

namespace tgfx {

// The shared acceptance walk: the multi-plane tail the color IR must carry (the YUV source leaf
// with the bounded color-grade operators above it) and the plane format the YUV_FORMAT dimension
// needs. A pending multi-plane upload lowers with the layout unknown, so the format resolves off
// the leaf's own proxy — the same view the processor wraps, read without the processor tree.
static bool CollectYUVFacts(const DrawShape& shape, int* yuvFormat) {
  // The precompiled vert has no texSubset attribute, so subset-carrying quads fall back.
  if (shape.geometry.kind != GeometryKind::QuadPerEdgeAA || shape.geometry.hasSubset) {
    return false;
  }
  if (shape.ir == nullptr) {
    return false;
  }
  AOTChainBuilder::AOTYUVTailShape tail = {};
  if (!AOTChainBuilder::CollectYUVTail(shape.ir->color, &tail)) {
    return false;
  }
  // No coverage processors (the runtime route stays authoritative for clipped video), and the
  // color processors are exactly the source plus the tail the IR carries — the staleness guard
  // keeps the tree and the graph in agreement.
  if (shape.colorFPCount < 1 || shape.colorFPCount > 1 + tail.ops.size() ||
      shape.colorFPCount != shape.fragmentCount) {
    return false;
  }
  int format = tail.leaf->yuvFormat;
  if (format < 0) {
    auto textureView =
        tail.leaf->textureProxy != nullptr ? tail.leaf->textureProxy->getTextureView() : nullptr;
    if (textureView == nullptr || !textureView->isYUV()) {
      return false;
    }
    auto* yuvView = static_cast<const YUVTextureView*>(textureView.get());
    if (yuvView->yuvFormat() == YUVFormat::I420) {
      format = 0;
    } else if (yuvView->yuvFormat() == YUVFormat::NV12) {
      format = 1;
    } else {
      return false;
    }
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  *yuvFormat = format;
  return true;
}

bool YUVTextureFillShader::Accept(const DrawShape& shape) {
  int yuvFormat = 0;
  return CollectYUVFacts(shape, &yuvFormat);
}

Variants YUVTextureFillShader::Select(const DrawShape& shape) {
  int yuvFormat = 0;
  // Accept already validated the tail on the match path; this second walk reads the format back.
  CollectYUVFacts(shape, &yuvFormat);
  return Variants()
      .vert("HAS_UV_COORD", shape.geometry.hasUVMatrix ? 0 : 1)
      .frag("YUV_FORMAT", yuvFormat)
      .frag(XP_DEFINE_NAME, shape.xpType());
}

}  // namespace tgfx
