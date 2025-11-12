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
#include "gpu/ProxyProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/VertexBufferView.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class AtlasTextOp final : public DrawOp {
 public:
  static PlacementPtr<AtlasTextOp> Make(Context* context,
                                        PlacementPtr<RectsVertexProvider> provider,
                                        uint32_t renderFlags,
                                        std::shared_ptr<TextureProxy> textureProxy,
                                        const SamplingOptions& sampling);

  bool hasCoverage() const override;

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::AtlasTextOp;
  }

 private:
  size_t rectCount = 0;
  std::optional<Color> commonColor = std::nullopt;
  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferView> vertexBufferProxyView = {};
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  SamplingOptions sampling{FilterMode::Nearest, MipmapMode::None};

  explicit AtlasTextOp(RectsVertexProvider* provider, std::shared_ptr<TextureProxy> textureProxy,
                       const SamplingOptions& sampling);

  friend class BlockAllocator;
};
}  // namespace tgfx
