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
                                                       AAType aa, std::optional<PMColor> commonColor,
                                                       const SamplingOptions& sampling)
    : GeometryProcessor(ClassID()), textureProxy(std::move(textureProxy)), commonColor(commonColor),
      samplerState(sampling) {
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
}
}  // namespace tgfx