/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "EllipseGeometryProcessor.h"

namespace tgfx {
EllipseGeometryProcessor::EllipseGeometryProcessor(int width, int height, bool stroke,
                                                   bool useScale)
    : GeometryProcessor(ClassID()), width(width), height(height), stroke(stroke),
      useScale(useScale) {
  inPosition = {"inPosition", SLType::Float2};
  inColor = {"inColor", SLType::Float4};
  if (useScale) {
    inEllipseOffset = {"inEllipseOffset", SLType::Float3};
  } else {
    inEllipseOffset = {"inEllipseOffset", SLType::Float2};
  }
  inEllipseRadii = {"inEllipseRadii", SLType::Float4};
  this->setVertexAttributes(&inPosition, 4);
}

void EllipseGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = stroke ? 1 : 0;
  bytesKey->write(flags);
}
}  // namespace tgfx
