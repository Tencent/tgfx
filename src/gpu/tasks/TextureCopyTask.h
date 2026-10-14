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

#include "RenderTask.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
/**
 * Copies the whole source texture into the destination texture at the same pixel positions. Both
 * textures must have the same size and format, and the source format must be renderable (the copy
 * reads the source through a framebuffer on OpenGL).
 */
class TextureCopyTask : public RenderTask {
 public:
  TextureCopyTask(BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
                  std::shared_ptr<TextureProxy> dest);

  void execute(CommandEncoder* encoder) override;

 private:
  std::shared_ptr<TextureProxy> source = nullptr;
  std::shared_ptr<TextureProxy> dest = nullptr;
};
}  // namespace tgfx
