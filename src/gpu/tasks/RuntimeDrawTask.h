/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "RenderTask.h"
#include "gpu/proxies/VertexBufferProxy.h"
#include "tgfx/gpu/RuntimeEffect.h"

namespace tgfx {
class RuntimeDrawTask : public RenderTask {
 public:
  RuntimeDrawTask(std::shared_ptr<RenderTargetProxy> target,
                  std::vector<std::shared_ptr<TextureProxy>> inputs,
                  std::shared_ptr<RuntimeEffect> effect, const Point& offset, const Point& scale);

  bool execute(RenderPass* renderPass) override;

 private:
  std::vector<std::shared_ptr<TextureProxy>> inputTextures = {};
  std::vector<std::shared_ptr<VertexBufferProxy>> inputVertexBuffers = {};
  std::shared_ptr<RuntimeEffect> effect = nullptr;
  Point offset = {};
  Point scale = {1.0f, 1.0f};

  static std::shared_ptr<Texture> GetFlatTexture(RenderPass* renderPass,
                                                 std::shared_ptr<TextureProxy> textureProxy,
                                                 std::shared_ptr<VertexBufferProxy> vertexProxy);
};
}  // namespace tgfx
