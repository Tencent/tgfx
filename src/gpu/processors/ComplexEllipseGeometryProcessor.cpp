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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ComplexEllipseGeometryProcessor.h"

namespace tgfx {
ComplexEllipseGeometryProcessor::ComplexEllipseGeometryProcessor(int width, int height, bool stroke,
                                                                 std::optional<PMColor> commonColor)
    : GeometryProcessor(ClassID()), width(width), height(height), stroke(stroke),
      commonColor(commonColor) {
  inPosition = {"inPosition", VertexFormat::Float2};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  inEllipseOffset = {"inEllipseOffset", VertexFormat::Float2};
  inEllipseRadii = {"inEllipseRadii", VertexFormat::Float4};
  inEdgeDist = {"inEdgeDist", VertexFormat::Float2};
  if (stroke) {
    inStrokeWidth = {"inStrokeWidth", VertexFormat::Float2};
  }
  this->setVertexAttributes(&inPosition, 6);
}

void ComplexEllipseGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = stroke ? 1 : 0;
  flags |= commonColor.has_value() ? 2 : 0;
  bytesKey->write(flags);
}
}  // namespace tgfx
