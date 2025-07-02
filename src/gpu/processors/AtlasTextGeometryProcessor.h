/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#pragma once

#include <optional>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
class AtlasTextGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<AtlasTextGeometryProcessor> Make(BlockBuffer* buffer,
                                                       std::shared_ptr<TextureProxy> textureProxy,
                                                       const SamplingOptions& sampling, AAType aa,
                                                       std::optional<Color> commonColor);
  std::string name() const override {
    return "AtlasTextGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  AtlasTextGeometryProcessor(std::shared_ptr<TextureProxy> textureProxy,
                             const SamplingOptions& sampling, AAType aa,
                             std::optional<Color> commonColor);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  const TextureSampler* onTextureSampler(size_t index) const override {
    DEBUG_ASSERT(index < textureSamplers.size());
    return textureSamplers[index];
  }

  SamplerState onSamplerState(size_t) const override {
    return samplerState;
  }

  Attribute position;  // May contain coverage as last channel
  Attribute coverage;
  Attribute maskCoord;
  Attribute color;

  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  SamplerState samplerState;
  AAType aa = AAType::None;
  std::optional<Color> commonColor = std::nullopt;
  std::vector<const TextureSampler*> textureSamplers;
};
}  // namespace tgfx
