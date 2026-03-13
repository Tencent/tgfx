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

#include "MetalDrawable.h"
#import <Metal/Metal.h>
#include "MetalCommandBuffer.h"
#include "MetalDrawableProxy.h"

namespace tgfx {

static PixelFormat MTLPixelFormatToPixelFormat(MTLPixelFormat format) {
  switch (format) {
    case MTLPixelFormatBGRA8Unorm:
      return PixelFormat::BGRA_8888;
    case MTLPixelFormatRGBA8Unorm:
      return PixelFormat::RGBA_8888;
    case MTLPixelFormatR8Unorm:
      return PixelFormat::ALPHA_8;
    case MTLPixelFormatRG8Unorm:
      return PixelFormat::RG_88;
    case MTLPixelFormatBGRA8Unorm_sRGB:
      return PixelFormat::BGRA_8888;
    case MTLPixelFormatRGBA8Unorm_sRGB:
      return PixelFormat::RGBA_8888;
    default:
      return PixelFormat::RGBA_8888;
  }
}

MetalDrawable::MetalDrawable(CAMetalLayer* layer, int width, int height,
                             std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(width, height, std::move(colorSpace)), metalLayer(layer) {
  pixelFormat = MTLPixelFormatToPixelFormat(layer.pixelFormat);
}

ImageOrigin MetalDrawable::onGetOrigin() const {
  return ImageOrigin::TopLeft;
}

PixelFormat MetalDrawable::onGetPixelFormat() const {
  return pixelFormat;
}

std::shared_ptr<RenderTargetProxy> MetalDrawable::onCreateProxy(Context* context) {
  return std::make_shared<MetalDrawableProxy>(context, width(), height(), metalLayer, pixelFormat);
}

void MetalDrawable::onPresent(Context*, std::shared_ptr<CommandBuffer> commandBuffer) {
  if (commandBuffer == nullptr || _proxy == nullptr) {
    return;
  }
  auto metalProxy = static_cast<MetalDrawableProxy*>(_proxy);
  auto drawable = metalProxy->getMetalDrawable();
  if (drawable == nil) {
    return;
  }
  auto metalCB = std::static_pointer_cast<MetalCommandBuffer>(commandBuffer);
  [metalCB->metalCommandBuffer() presentDrawable:drawable];
}
}  // namespace tgfx
