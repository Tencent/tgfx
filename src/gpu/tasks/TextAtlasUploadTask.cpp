/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextAtlasUploadTask.h"
#include "core/AtlasTypes.h"
#include "core/utils/AutoMalloc.h"
#include "gpu/Gpu.h"
#include "gpu/Texture.h"

namespace tgfx {

static void getPackedGlyphImage(const void* srcPixels, void* dstPixels,
                                const ImageInfo& srcImageInfo, size_t dstRowBytes) {
  auto srcHeight = srcImageInfo.height();
  auto srcRowBytes = srcImageInfo.rowBytes();
  for (auto y = 0; y < srcHeight; y++) {
    memcpy(dstPixels, srcPixels, srcRowBytes);
    srcPixels = static_cast<const char*>(srcPixels) + srcRowBytes;
    dstPixels = static_cast<char*>(dstPixels) + dstRowBytes;
  }
}

TextAtlasUploadTask::TextAtlasUploadTask(UniqueKey uniqueKey,
                                         std::shared_ptr<DataSource<PixelBuffer>> source,
                                         std::shared_ptr<TextureProxy> textureProxy,
                                         Point atlasOffset)
    : ResourceTask(std::move(uniqueKey)), source(std::move(source)),
      textureProxy(std::move(textureProxy)), atlasOffset(atlasOffset) {
}

bool TextAtlasUploadTask::execute(Context* context) {
  if (source == nullptr || textureProxy == nullptr) {
    return false;
  }
  auto pixelBuffer = source->getData();
  if (pixelBuffer == nullptr) {
    LOGE("TextAtlasUploadTask::execute() pixelBuffer is nullptr!");
    return false;
  }
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    LOGE("TextAtlasUploadTask::onMakeResource() texture is nullptr!");
    return false;
  }
  auto gpu = context->gpu();
  auto pixels = pixelBuffer->lockPixels();
  if (pixels == nullptr) {
    LOGE("TextAtlasUploadTask::execute() lockPixels is nullptr!");
    return false;
  }
  auto rect = Rect::MakeXYWH(atlasOffset.x, atlasOffset.y, static_cast<float>(pixelBuffer->width()),
                             static_cast<float>(pixelBuffer->height()));
  auto padding = static_cast<float>(Plot::CellPadding);
  rect.outset(padding, padding);
  auto dstRowBytes = static_cast<size_t>(rect.width()) * pixelBuffer->info().bytesPerPixel();
  auto size = static_cast<size_t>(rect.height()) * dstRowBytes;
  AutoMalloc<1024> storage(size);
  void* dstPixels = storage.get();
  memset(dstPixels, 0, size);
  dstPixels = static_cast<char*>(dstPixels) + dstRowBytes + pixelBuffer->info().bytesPerPixel();
  getPackedGlyphImage(pixels, dstPixels, pixelBuffer->info(), dstRowBytes);
  gpu->writePixels(texture->getSampler(), rect, storage.get(), dstRowBytes);
  pixelBuffer->unlockPixels();
  source = nullptr;
  return true;
}
}  // namespace tgfx
