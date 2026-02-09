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

#include "NonAARRectGeometryProcessor.h"

namespace tgfx {
NonAARRectGeometryProcessor::NonAARRectGeometryProcessor(int width, int height, bool stroke,
                                                         std::optional<PMColor> commonColor)
    : GeometryProcessor(ClassID()), width(width), height(height), stroke(stroke),
      commonColor(commonColor) {
  // Attributes must be declared in the same order as vertex data layout.
  // Vertex data layout:
  // - position (2 floats)
  // - localCoord (2 floats)
  // - radii (2 floats)
  // - rectBounds (4 floats)
  // - strokeWidth (2 floats, stroke only)
  // - color (1 float as UByte4Normalized, optional)
  inPosition = {"inPosition", VertexFormat::Float2};
  inLocalCoord = {"inLocalCoord", VertexFormat::Float2};
  inRadii = {"inRadii", VertexFormat::Float2};
  inRectBounds = {"inRectBounds", VertexFormat::Float4};
  if (stroke) {
    inStrokeWidth = {"inStrokeWidth", VertexFormat::Float2};
    if (!commonColor.has_value()) {
      inColor = {"inColor", VertexFormat::UByte4Normalized};
    }
    // stroke mode: 5 or 6 attributes
    this->setVertexAttributes(&inPosition, commonColor.has_value() ? 5 : 6);
  } else {
    if (!commonColor.has_value()) {
      // fill mode without common color: need to skip strokeWidth in attribute array
      // Use inStrokeWidth slot for inColor since strokeWidth is not used
      inStrokeWidth = {"inColor", VertexFormat::UByte4Normalized};
    }
    // fill mode: 4 or 5 attributes (inStrokeWidth holds inColor when hasColor is true)
    this->setVertexAttributes(&inPosition, commonColor.has_value() ? 4 : 5);
  }
}

void NonAARRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = commonColor.has_value() ? 1 : 0;
  flags |= stroke ? 2 : 0;
  bytesKey->write(flags);
}
}  // namespace tgfx
