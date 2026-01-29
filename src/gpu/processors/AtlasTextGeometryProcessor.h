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

#pragma once

#include <optional>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
class AtlasTextGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<AtlasTextGeometryProcessor> Make(BlockAllocator* allocator,
                                                       std::shared_ptr<TextureProxy> textureProxy,
                                                       AAType aa,
                                                       std::optional<PMColor> commonColor,
                                                       const SamplingOptions& sampling,
                                                       bool forceAsMask = false);
  std::string name() const override {
    return "AtlasTextGeometryProcessor";
  }

  SamplerState onSamplerStateAt(size_t) const override {
    return samplerState;
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  AtlasTextGeometryProcessor(std::shared_ptr<TextureProxy> textureProxy, AAType aa,
                             std::optional<PMColor> commonColor, const SamplingOptions& sampling,
                             bool forceAsMask);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  std::shared_ptr<Texture> onTextureAt(size_t index) const override {
    DEBUG_ASSERT(index < textures.size());
    return textures[index];
  }

  Attribute position;  // May contain coverage as last channel
  Attribute coverage;
  Attribute maskCoord;
  Attribute color;

  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  AAType aa = AAType::None;
  std::optional<PMColor> commonColor = std::nullopt;
  bool forceAsMask = false;
  std::vector<std::shared_ptr<Texture>> textures;
  SamplerState samplerState = {};
};
}  // namespace tgfx
