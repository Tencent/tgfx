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
SmoothRRectGeometryProcessor::SmoothRRectGeometryProcessor(int width, int height, float smoothness,
                                                           bool antiAlias,
                                                           std::optional<PMColor> commonColor,
                                                           bool elliptical, bool stroke,
                                                           bool localStroke)
    : GeometryProcessor(ClassID()), width(width), height(height), smoothness(smoothness),
      antiAlias(antiAlias), elliptical(elliptical), stroke(stroke), localStroke(localStroke),
      commonColor(commonColor) {
  inPosition = {"inPosition", VertexFormat::Float2};
  inLocalCoord = {"inLocalCoord", VertexFormat::Float2};
  inRadii = {"inRadii", VertexFormat::Float4};
  if (elliptical) {
    inRadiiY = {"inRadiiY", VertexFormat::Float4};
  }
  inRectBounds = {"inRectBounds", VertexFormat::Float4};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  if (stroke) {
    inStrokeWidth = {"inStrokeWidth", VertexFormat::Float};
  }
  if (localStroke) {
    inInvTranspose = {"inInvTranspose", VertexFormat::Float4};
  }
  // setVertexAttributes skips empty attributes, so pass the full member span: variant-dependent
  // members (inRadiiY, inColor, inStrokeWidth, inInvTranspose) may be uninitialized.
  this->setVertexAttributes(&inPosition, 8);
}

void SmoothRRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // The smoothness value itself stays a uniform and is deliberately left out, otherwise every
  // distinct value would produce its own program. The elliptical, stroke and localStroke flags
  // change the vertex layout and the generated code, so they must take part in the key.
  uint32_t key = commonColor.has_value() ? 1u : 0u;
  key |= elliptical ? 2u : 0u;
  key |= stroke ? 4u : 0u;
  key |= localStroke ? 8u : 0u;
  bytesKey->write(key);
}
}  // namespace tgfx
