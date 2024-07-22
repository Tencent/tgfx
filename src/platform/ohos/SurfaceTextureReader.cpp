/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/platform/ohos/SurfaceTextureReader.h"
#include <chrono>
#include "platform/ohos/SurfaceTexture.h"

namespace tgfx {
std::shared_ptr<SurfaceTextureReader> SurfaceTextureReader::Make(int width, int height) {
  if (width < 1 || height < 1) {
    return nullptr;
  }
  auto imageStream = SurfaceTexture::Make(width, height);
  if (imageStream == nullptr) {
    return nullptr;
  }
  auto imageReader =
      std::shared_ptr<SurfaceTextureReader>(new SurfaceTextureReader(std::move(imageStream)));
  imageReader->weakThis = imageReader;
  return imageReader;
}

SurfaceTextureReader::SurfaceTextureReader(std::shared_ptr<ImageStream> stream)
    : ImageReader(std::move(stream)) {
}

NativeWindow* SurfaceTextureReader::getInputSurface() const {
  return std::static_pointer_cast<SurfaceTexture>(stream)->getInputSurface();
}

std::shared_ptr<ImageBuffer> SurfaceTextureReader::acquireNextBuffer() {
  stream->markContentDirty(Rect::MakeWH(stream->width(), stream->height()));
  return ImageReader::acquireNextBuffer();
}
}  // namespace tgfx
