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
#include "gpu/RectsVertexProvider.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/GpuBufferProxy.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class AtlasTextOp final : public DrawOp {
 public:
  static PlacementPtr<AtlasTextOp> Make(Context* context,
                                        PlacementPtr<RectsVertexProvider> provider,
                                        uint32_t renderFlags,
                                        std::shared_ptr<TextureProxy> textureProxy,
                                        const SamplingOptions& sampling);
  void execute(RenderPass* renderPass) override;

 private:
  size_t rectCount = 0;
  std::optional<Color> commonColor = std::nullopt;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy = nullptr;
  size_t vertexBufferOffset = 0;
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  SamplingOptions sampling;

  explicit AtlasTextOp(RectsVertexProvider* provider, std::shared_ptr<TextureProxy> textureProxy,
                       const SamplingOptions& sampling);

  friend class BlockBuffer;
};
}  // namespace tgfx
