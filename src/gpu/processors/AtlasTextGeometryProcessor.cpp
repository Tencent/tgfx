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

#include "AtlasTextGeometryProcessor.h"

namespace tgfx {
AtlasTextGeometryProcessor::AtlasTextGeometryProcessor(std::shared_ptr<TextureProxy> textureProxy,
                                                       AAType aa, std::optional<Color> commonColor,
                                                       const SamplingOptions& sampling,
                                                       std::shared_ptr<ColorSpace> colorSpace)
    : GeometryProcessor(ClassID()), textureProxy(std::move(textureProxy)), commonColor(commonColor),
      samplerState(sampling), dstColorSpace(std::move(colorSpace)) {
  position = {"aPosition", VertexFormat::Float2};
  if (aa == AAType::Coverage) {
    coverage = {"inCoverage", VertexFormat::Float};
  }
  maskCoord = {"maskCoord", VertexFormat::Float2};
  if (!commonColor.has_value()) {
    color = {"inColor", VertexFormat::UByte4Normalized};
  }
  setVertexAttributes(&position, 4);
  textures.emplace_back(this->textureProxy->getTextureView()->getTexture());
  setTextureSamplerCount(textures.size());
}
void AtlasTextGeometryProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t flags = aa == AAType::Coverage ? 1 : 0;
  flags |= commonColor.has_value() ? 2 : 0;
  flags |= textureProxy->isAlphaOnly() ? 4 : 0;
  bytesKey->write(flags);
  if(!textureProxy->isAlphaOnly()) {
    auto srcColorSpace = textureProxy->getTextureView()->colorSpace();
    auto steps = std::make_shared<ColorSpaceXformSteps>(
        srcColorSpace.get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
    auto key = ColorSpaceXformSteps::XFormKey(steps.get());
    bytesKey->write(key);
  }
  if (!commonColor.has_value()) {
    auto vertSteps = std::make_shared<ColorSpaceXformSteps>(
        ColorSpace::MakeSRGB().get(), AlphaType::Premultiplied, dstColorSpace.get(),
        AlphaType::Premultiplied);
    auto vertxKey = ColorSpaceXformSteps::XFormKey(vertSteps.get());
    bytesKey->write(vertxKey);
  }
}
}  // namespace tgfx