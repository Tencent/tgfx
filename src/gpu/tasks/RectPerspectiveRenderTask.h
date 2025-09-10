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

#include "gpu/AAType.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {

class RectPerspectiveRenderTask final : public RenderTask {
 public:
  RectPerspectiveRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget, const Rect& rect,
                            AAType aa, std::shared_ptr<TextureProxy> fillTexture)
      : renderTarget(std::move(renderTarget)), rect(rect), aa(aa),
        fillTexture(std::move(fillTexture)) {
  }

  void execute(CommandEncoder* encoder) override;

 private:
  /**
   * The maximum number of vertices per non-AA quad.
   */
  static constexpr uint16_t IndicesPerNonAAQuad = 6;

  /**
   * The maximum number of vertices per AA quad.
   */
  static constexpr uint16_t IndicesPerAAQuad = 30;

  std::shared_ptr<RenderTargetProxy> renderTarget = nullptr;
  Rect rect;
  AAType aa = AAType::None;
  std::shared_ptr<TextureProxy> fillTexture = nullptr;
};

}  // namespace tgfx