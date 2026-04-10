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

#include <filesystem>
#include <fstream>
#include "core/images/TextureImage.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
static const std::string DEBUG_OUT_ROOT = "/Users/chenjie/Desktop/Develop/TGFX_WT_0/test/out/";

inline void DebugSaveFile(const std::shared_ptr<Data>& data, const std::string& name) {
  const std::filesystem::path path = DEBUG_OUT_ROOT + "/" + name;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out.write(static_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
  out.close();
}

inline void DebugSavePixmap(const Pixmap& pixmap, const std::string& name) {
  const auto data = ImageCodec::Encode(pixmap, EncodedFormat::WEBP, 100);
  if (data == nullptr) {
    return;
  }
  DebugSaveFile(data, name + ".webp");
}

inline bool DebugSaveSurface(Surface* surface, const std::string& name) {
  if (surface == nullptr) {
    return false;
  }
  Bitmap bitmap(surface->width(), surface->height(), false, false, surface->colorSpace());
  Pixmap pixmap(bitmap);
  if (!surface->readPixels(pixmap.info(), pixmap.writablePixels())) {
    return false;
  }
  DebugSavePixmap(pixmap, name);
  return true;
}

inline bool DebugSaveCanvas(Canvas* canvas, const std::string& name) {
  if (canvas == nullptr) {
    return false;
  }
  return DebugSaveSurface(canvas->getSurface(), name);
}

inline bool DebugSaveImage(const std::shared_ptr<Image>& image, Context* context,
                           const std::string& name) {
  if (image == nullptr || context == nullptr) {
    return false;
  }
  context->flushAndSubmit();
  const auto surface = Surface::Make(context, image->width(), image->height());
  if (surface == nullptr) {
    return false;
  }
  surface->getCanvas()->drawImage(image);
  return DebugSaveSurface(surface.get(), name);
}

inline bool DebugSaveTextureProxy(const std::shared_ptr<TextureProxy>& textureProxy,
                                  const std::string& name) {
  if (textureProxy == nullptr) {
    return false;
  }
  const auto image = TextureImage::Wrap(textureProxy, nullptr);
  return DebugSaveImage(image, textureProxy->getContext(), name);
}

// inline bool DebugSaveRenderTargetProxy(...) omitted - Surface::MakeFrom is private
}  // namespace tgfx
