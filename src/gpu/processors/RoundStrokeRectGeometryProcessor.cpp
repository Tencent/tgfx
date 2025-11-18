/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "RoundStrokeRectGeometryProcessor.h"

namespace tgfx {
RoundStrokeRectGeometryProcessor::RoundStrokeRectGeometryProcessor(AAType aaType,
                                                                   std::optional<PMColor> commonColor,
                                                                   std::optional<Matrix> uvMatrix)
    : GeometryProcessor(ClassID()), aaType(aaType), commonColor(commonColor), uvMatrix(uvMatrix) {
  inPosition = {"inPosition", VertexFormat::Float2};
  if (aaType == AAType::Coverage) {
    inCoverage = {"inCoverage", VertexFormat::Float};
  }
  inEllipseOffset = {"inEllipseOffset", VertexFormat::Float2};
  if (aaType == AAType::Coverage) {
    inEllipseRadii = {"inEllipseRadii", VertexFormat::Float2};
  }
  if (!uvMatrix.has_value()) {
    inUVCoord = {"inUVCoord", VertexFormat::Float2};
  }
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  setVertexAttributes(&inPosition, 6);
}

void RoundStrokeRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = aaType == AAType::Coverage ? 1 : 0;
  flags |= commonColor.has_value() ? 2 : 0;
  flags |= uvMatrix.has_value() ? 4 : 0;
  bytesKey->write(flags);
}

}  // namespace tgfx
