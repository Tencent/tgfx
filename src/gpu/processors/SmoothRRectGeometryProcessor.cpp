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
                                                           std::optional<PMColor> commonColor)
    : GeometryProcessor(ClassID()), width(width), height(height), smoothness(smoothness),
      antiAlias(antiAlias), commonColor(commonColor) {
  inPosition = {"inPosition", VertexFormat::Float2};
  inLocalCoord = {"inLocalCoord", VertexFormat::Float2};
  inRadii = {"inRadii", VertexFormat::Float4};
  inRectBounds = {"inRectBounds", VertexFormat::Float4};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  this->setVertexAttributes(&inPosition, 5);
}

void SmoothRRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // The smoothness value itself stays a uniform and is deliberately left out, otherwise every
  // distinct value would produce its own program.
  bytesKey->write(static_cast<uint32_t>(commonColor.has_value() ? 1 : 0));
}
}  // namespace tgfx
