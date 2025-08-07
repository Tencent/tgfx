/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "YUVTextureView.h"
#include "core/utils/Log.h"
#include "gpu/GPU.h"

namespace tgfx {
static constexpr int YUV_SIZE_FACTORS[] = {0, 1, 1};

static std::vector<std::unique_ptr<GPUTexture>> MakeTexturePlanes(Context* context,
                                                                  const YUVData* yuvData,
                                                                  const PixelFormat* formats) {
  std::vector<std::unique_ptr<GPUTexture>> texturePlanes = {};
  auto count = static_cast<int>(yuvData->planeCount());
  for (int index = 0; index < count; index++) {
    auto w = yuvData->width() >> YUV_SIZE_FACTORS[index];
    auto h = yuvData->height() >> YUV_SIZE_FACTORS[index];
    auto texture = GPUTexture::Make(context, w, h, formats[index]);
    if (texture == nullptr) {
      for (auto& plane : texturePlanes) {
        plane->releaseGPU(context);
      }
      return {};
    }
    texturePlanes.push_back(std::move(texture));
  }
  return texturePlanes;
}

static void SubmitYUVTexture(Context* context, const YUVData* yuvData,
                             std::unique_ptr<GPUTexture> textures[]) {
  auto count = yuvData->planeCount();
  for (size_t index = 0; index < count; index++) {
    auto& texture = textures[index];
    auto w = yuvData->width() >> YUV_SIZE_FACTORS[index];
    auto h = yuvData->height() >> YUV_SIZE_FACTORS[index];
    auto pixels = yuvData->getBaseAddressAt(index);
    auto rowBytes = yuvData->getRowBytesAt(index);
    texture->writePixels(context, Rect::MakeWH(w, h), pixels, rowBytes);
    // YUV textures do not support mipmaps, so we don't need to regenerate mipmaps.
  }
}

std::shared_ptr<TextureView> TextureView::MakeI420(Context* context, const YUVData* yuvData,
                                                   YUVColorSpace colorSpace) {
  if (context == nullptr || yuvData == nullptr ||
      yuvData->planeCount() != YUVData::I420_PLANE_COUNT) {
    return nullptr;
  }
  PixelFormat yuvFormats[YUVData::I420_PLANE_COUNT] = {PixelFormat::GRAY_8, PixelFormat::GRAY_8,
                                                       PixelFormat::GRAY_8};
  auto texturePlanes = MakeTexturePlanes(context, yuvData, yuvFormats);
  if (texturePlanes.empty()) {
    return nullptr;
  }
  auto yuvTexture = new YUVTextureView(std::move(texturePlanes), yuvData->width(),
                                       yuvData->height(), YUVFormat::I420, colorSpace);
  auto texture =
      std::static_pointer_cast<YUVTextureView>(Resource::AddToCache(context, yuvTexture));
  SubmitYUVTexture(context, yuvData, texture->textures.data());
  return texture;
}

std::shared_ptr<TextureView> TextureView::MakeNV12(Context* context, const YUVData* yuvData,
                                                   YUVColorSpace colorSpace) {
  if (context == nullptr || yuvData == nullptr ||
      yuvData->planeCount() != YUVData::NV12_PLANE_COUNT) {
    return nullptr;
  }
  PixelFormat yuvFormats[YUVData::NV12_PLANE_COUNT] = {PixelFormat::GRAY_8, PixelFormat::RG_88};
  auto texturePlanes = MakeTexturePlanes(context, yuvData, yuvFormats);
  if (texturePlanes.empty()) {
    return nullptr;
  }
  auto yuvTexture = new YUVTextureView(std::move(texturePlanes), yuvData->width(),
                                       yuvData->height(), YUVFormat::NV12, colorSpace);
  auto texture =
      std::static_pointer_cast<YUVTextureView>(Resource::AddToCache(context, yuvTexture));
  SubmitYUVTexture(context, yuvData, texture->textures.data());
  return texture;
}

YUVTextureView::YUVTextureView(std::vector<std::unique_ptr<GPUTexture>> yuvTextures, int width,
                               int height, YUVFormat yuvFormat, YUVColorSpace colorSpace)
    : TextureView(width, height, ImageOrigin::TopLeft), _yuvFormat(yuvFormat),
      _colorSpace(colorSpace) {
  DEBUG_ASSERT(_yuvFormat != YUVFormat::Unknown);
  DEBUG_ASSERT(yuvTextures.size() == textureCount());
  for (size_t i = 0; i < yuvTextures.size(); ++i) {
    textures[i] = std::move(yuvTextures[i]);
  }
}

size_t YUVTextureView::textureCount() const {
  switch (_yuvFormat) {
    case YUVFormat::I420:
      return YUVData::I420_PLANE_COUNT;
    case YUVFormat::NV12:
      return YUVData::NV12_PLANE_COUNT;
    default:
      DEBUG_ASSERT(false);
      return 0;
  }
}

GPUTexture* YUVTextureView::getTextureAt(size_t index) const {
  DEBUG_ASSERT(index < textureCount());
  return textures[index].get();
}

size_t YUVTextureView::memoryUsage() const {
  if (auto hardwareBuffer = textures.front()->getHardwareBuffer()) {
    return HardwareBufferGetInfo(hardwareBuffer).byteSize();
  }
  return static_cast<size_t>(_width) * static_cast<size_t>(_height) * 3 / 2;
}

Point YUVTextureView::getTextureCoord(float x, float y) const {
  return {x / static_cast<float>(_width), y / static_cast<float>(_height)};
}

void YUVTextureView::onReleaseGPU() {
  auto count = textureCount();
  for (size_t i = 0; i < count; ++i) {
    textures[i]->releaseGPU(context);
  }
}
}  // namespace tgfx
