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

#include "ExternalRenderTarget.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     const BackendRenderTarget& backendRenderTarget,
                                                     ImageOrigin origin) {
  if (context == nullptr) {
    return nullptr;
  }
  auto texture = context->gpu()->importBackendRenderTarget(backendRenderTarget);
  if (!texture) {
    return nullptr;
  }
  auto renderTarget = new ExternalRenderTarget(std::move(texture), origin);
  return Resource::AddToCache(context, renderTarget);
}

}  // namespace tgfx
