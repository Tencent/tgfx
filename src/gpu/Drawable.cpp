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

#include "tgfx/gpu/Drawable.h"
#include "core/utils/Log.h"
#include "gpu/proxies/DrawableProxy.h"
#include "inspect/InspectorMark.h"

namespace tgfx {
PixelFormat Drawable::onGetPixelFormat() const {
  return PixelFormat::RGBA_8888;
}

ImageOrigin Drawable::onGetOrigin() const {
  return ImageOrigin::BottomLeft;
}

int Drawable::onGetSampleCount() const {
  return 1;
}

std::shared_ptr<RenderTarget> Drawable::onCreateRenderTarget(Context*) {
  return nullptr;
}

std::shared_ptr<DrawableProxy> Drawable::getProxy(Context* context) {
  if (_proxyHolder == nullptr) {
    _proxyHolder = onCreateProxy(context);
    if (_proxyHolder != nullptr) {
      _proxy = _proxyHolder.get();
    }
  }
  return _proxyHolder;
}

std::shared_ptr<DrawableProxy> Drawable::onCreateProxy(Context* context) {
  return std::make_shared<DrawableProxy>(context, this);
}

void Drawable::present(Context* context, std::shared_ptr<CommandBuffer> commandBuffer) {
  FRAME_MARK;
  DEBUG_ASSERT(context != nullptr);
  onPresent(context, std::move(commandBuffer));
}
}  // namespace tgfx
