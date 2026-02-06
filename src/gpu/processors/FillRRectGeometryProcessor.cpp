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

#include "FillRRectGeometryProcessor.h"

namespace tgfx {
FillRRectGeometryProcessor::FillRRectGeometryProcessor(int width, int height, AAType aaType,
                                                       std::optional<PMColor> commonColor)
    : GeometryProcessor(ClassID()), width(width), height(height), aaType(aaType),
      commonColor(commonColor) {
  inCornerAndRadius = {"inCornerAndRadius", VertexFormat::Float4};
  inAABloatCoverage = {"inAABloatCoverage", VertexFormat::Float4};
  inSkew = {"inSkew", VertexFormat::Float4};
  inTranslate = {"inTranslate", VertexFormat::Float2};
  inRadii = {"inRadii", VertexFormat::Float2};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
    this->setVertexAttributes(&inCornerAndRadius, 6);
  } else {
    this->setVertexAttributes(&inCornerAndRadius, 5);
  }
}

void FillRRectGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = static_cast<uint32_t>(aaType);
  flags |= commonColor.has_value() ? (1 << 2) : 0;
  bytesKey->write(flags);
}
}  // namespace tgfx
