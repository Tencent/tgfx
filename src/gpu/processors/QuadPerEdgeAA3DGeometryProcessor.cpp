/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "QuadPerEdgeAA3DGeometryProcessor.h"

namespace tgfx {

QuadPerEdgeAA3DGeometryProcessor::QuadPerEdgeAA3DGeometryProcessor(
    AAType aa, const Matrix3D& transform, const Vec2& ndcScale, const Vec2& ndcOffset,
    std::optional<PMColor> commonColor)
    : GeometryProcessor(ClassID()), aa(aa), matrix(transform), ndcScale(ndcScale),
      ndcOffset(ndcOffset), commonColor(commonColor) {
  position = {"aPosition", VertexFormat::Float2};
  if (aa == AAType::Coverage) {
    coverage = {"inCoverage", VertexFormat::Float};
  }
  if (!commonColor.has_value()) {
    color = {"inColor", VertexFormat::UByte4Normalized};
  }
  setVertexAttributes(&position, 3);
}

void QuadPerEdgeAA3DGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = (aa == AAType::Coverage ? 1 : 0);
  flags |= commonColor.has_value() ? 2 : 0;
  bytesKey->write(flags);
}

}  // namespace tgfx
