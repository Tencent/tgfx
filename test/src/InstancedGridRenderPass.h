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

#pragma once

#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/CommandEncoder.h"

namespace tgfx {
class InstancedGridRenderPass {
 public:
  static constexpr float GRID_SIZE = 20.0f;
  static constexpr float GRID_SPACING = 8.0f;

  static std::shared_ptr<InstancedGridRenderPass> Make(uint32_t rows, uint32_t columns);

  bool onDraw(CommandEncoder* encoder, std::shared_ptr<Texture> renderTexture) const;

 private:
  InstancedGridRenderPass(uint32_t rows, uint32_t columns);

  uint32_t rows;
  uint32_t columns;
  Attribute position;
  Attribute offset;
  Attribute color;

  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu) const;
};
}  // namespace tgfx
