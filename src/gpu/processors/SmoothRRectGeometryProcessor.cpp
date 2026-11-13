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

#include "SmoothRRectGeometryProcessor.h"

namespace tgfx {
SmoothRRectGeometryProcessor::SmoothRRectGeometryProcessor(int width, int height, bool antiAlias,
                                                           std::optional<PMColor> commonColor,
                                                           bool stroke, bool unscaledGeometry)
    : GeometryProcessor(ClassID()), width(width), height(height), antiAlias(antiAlias),
      stroke(stroke), unscaledGeometry(unscaledGeometry), commonColor(commonColor) {
  inPosition = {"inPosition", VertexFormat::Float2};
  inLocalCoord = {"inLocalCoord", VertexFormat::Float2};
  inXRadii = {"inXRadii", VertexFormat::Float4};
  inYRadii = {"inYRadii", VertexFormat::Float4};
  inSmoothness = {"inSmoothness", VertexFormat::Float};
  inRectBounds = {"inRectBounds", VertexFormat::Float4};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  if (stroke) {
    inHalfStrokeWidth = {"inHalfStrokeWidth", VertexFormat::Float};
  }
  if (unscaledGeometry && antiAlias) {
    inInvScale = {"inInvScale", VertexFormat::Float2};
  }
  this->setVertexAttributes(&inPosition, 9);
}

void SmoothRRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // The antiAlias, stroke and unscaledGeometry flags change the generated code, so they must take
  // part in the key.
  uint32_t key = commonColor.has_value() ? 1u : 0u;
  key |= stroke ? 2u : 0u;
  key |= unscaledGeometry ? 4u : 0u;
  key |= antiAlias ? 8u : 0u;
  bytesKey->write(key);
}
}  // namespace tgfx
