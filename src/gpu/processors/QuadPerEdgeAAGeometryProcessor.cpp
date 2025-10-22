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

#include "QuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {
QuadPerEdgeAAGeometryProcessor::QuadPerEdgeAAGeometryProcessor(int width, int height, AAType aa,
                                                               std::optional<Color> commonColor,
                                                               std::optional<Matrix> uvMatrix,
                                                               bool hasSubset)
    : GeometryProcessor(ClassID()), width(width), height(height), aa(aa), commonColor(commonColor),
      uvMatrix(uvMatrix), hasSubset(hasSubset) {
  position = {"aPosition", VertexFormat::Float2};
  if (aa == AAType::Coverage) {
    coverage = {"inCoverage", VertexFormat::Float};
  }
  if (!uvMatrix.has_value()) {
    uvCoord = {"uvCoord", VertexFormat::Float2};
  }
  if (!commonColor.has_value()) {
    color = {"inColor", VertexFormat::UByte4Normalized};
  }
  if (hasSubset) {
    subset = {"texSubset", VertexFormat::Float4};
  }
  setVertexAttributes(&position, 5);
}

void QuadPerEdgeAAGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = aa == AAType::Coverage ? 1 : 0;
  flags |= commonColor.has_value() ? 2 : 0;
  flags |= uvMatrix.has_value() ? 4 : 0;
  flags |= hasSubset ? 8 : 0;
  bool hasSubsetMatrix = hasSubset && uvMatrix.has_value();
  flags |= hasSubsetMatrix ? 16 : 0;
  bytesKey->write(flags);
}
}  // namespace tgfx
