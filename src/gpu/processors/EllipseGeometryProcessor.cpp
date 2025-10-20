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

#include "EllipseGeometryProcessor.h"

namespace tgfx {
EllipseGeometryProcessor::EllipseGeometryProcessor(int width, int height, bool stroke,
                                                   bool useScale, std::optional<Color> commonColor,
                                                   std::shared_ptr<ColorSpace> colorSpace)
    : GeometryProcessor(ClassID()), width(width), height(height), stroke(stroke),
      useScale(useScale), commonColor(commonColor), dstColorSpace(std::move(colorSpace)) {
  inPosition = {"inPosition", VertexFormat::Float2};
  if (!commonColor.has_value()) {
    inColor = {"inColor", VertexFormat::UByte4Normalized};
  }
  if (useScale) {
    inEllipseOffset = {"inEllipseOffset", VertexFormat::Float3};
  } else {
    inEllipseOffset = {"inEllipseOffset", VertexFormat::Float2};
  }
  inEllipseRadii = {"inEllipseRadii", VertexFormat::Float4};
  this->setVertexAttributes(&inPosition, 4);
}

void EllipseGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = stroke ? 1 : 0;
  flags |= commonColor.has_value() ? 2 : 0;
  bytesKey->write(flags);
  if (!commonColor.has_value()) {
    auto steps = std::make_shared<ColorSpaceXformSteps>(
        ColorSpace::MakeSRGB().get(), AlphaType::Premultiplied, dstColorSpace.get(),
        AlphaType::Premultiplied);
    auto xformKey = ColorSpaceXformSteps::XFormKey(steps.get());
    uint32_t* key = reinterpret_cast<uint32_t*>(&xformKey);
    bytesKey->write(key[0]);
    bytesKey->write(key[1]);
  }
}
}  // namespace tgfx
