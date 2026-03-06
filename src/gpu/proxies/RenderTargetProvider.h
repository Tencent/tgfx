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

#include <memory>

namespace tgfx {
class Context;
class RenderTarget;

/**
 * An interface for providing render targets on demand. Implementations can defer the actual render
 * target creation until it is needed, for example, acquiring a Metal drawable at flush time.
 */
class RenderTargetProvider {
 public:
  virtual ~RenderTargetProvider() = default;

  /**
   * Creates or retrieves a render target. Called by DelayRenderTargetProxy when the render target is
   * first needed during a frame.
   */
  virtual std::shared_ptr<RenderTarget> getRenderTarget(Context* context) = 0;
};
}  // namespace tgfx
