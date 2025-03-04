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

#include "YUVTexture.h"
#include "core/utils/UniqueID.h"
#include "gpu/Gpu.h"

namespace tgfx {
static constexpr int YUV_SIZE_FACTORS[] = {0, 1, 1};

static std::vector<std::unique_ptr<TextureSampler>> MakeTexturePlanes(Context* context,
                                                                      const YUVData* yuvData,
                                                                      const PixelFormat* formats) {
  std::vector<std::unique_ptr<TextureSampler>> texturePlanes = {};
  auto count = static_cast<int>(yuvData->planeCount());
  for (int index = 0; index < count; index++) {
    auto w = yuvData->width() >> YUV_SIZE_FACTORS[index];
    auto h = yuvData->height() >> YUV_SIZE_FACTORS[index];
    auto sampler = context->gpu()->createSampler(w, h, formats[index], 1);
    if (sampler == nullptr) {
      for (auto& plane : texturePlanes) {
        context->gpu()->deleteSampler(plane.get());
      }
      return {};
    }
    texturePlanes.push_back(std::move(sampler));
  }
  return texturePlanes;
}

static void SubmitYUVTexture(Context* context, const YUVData* yuvData,
                             const std::vector<std::unique_ptr<TextureSampler>>* samplers) {
  auto count = yuvData->planeCount();
  for (size_t index = 0; index < count; index++) {
    auto sampler = (*samplers)[index].get();
    auto w = yuvData->width() >> YUV_SIZE_FACTORS[index];
    auto h = yuvData->height() >> YUV_SIZE_FACTORS[index];
    auto pixels = yuvData->getBaseAddressAt(index);
    auto rowBytes = yuvData->getRowBytesAt(index);
    context->gpu()->writePixels(sampler, Rect::MakeWH(w, h), pixels, rowBytes);
  }
}

std::shared_ptr<Texture> Texture::MakeI420(Context* context, const YUVData* yuvData,
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
  auto yuvTexture =
      new YUVTexture(yuvData->width(), yuvData->height(), YUVPixelFormat::I420, colorSpace);
  yuvTexture->samplers = std::move(texturePlanes);
  auto texture = std::static_pointer_cast<YUVTexture>(Resource::AddToCache(context, yuvTexture));
  SubmitYUVTexture(context, yuvData, &texture->samplers);
  return texture;
}

std::shared_ptr<Texture> Texture::MakeNV12(Context* context, const YUVData* yuvData,
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
  auto yuvTexture =
      new YUVTexture(yuvData->width(), yuvData->height(), YUVPixelFormat::NV12, colorSpace);
  yuvTexture->samplers = std::move(texturePlanes);
  auto texture = std::static_pointer_cast<YUVTexture>(Resource::AddToCache(context, yuvTexture));
  SubmitYUVTexture(context, yuvData, &texture->samplers);
  return texture;
}

YUVTexture::YUVTexture(int width, int height, YUVPixelFormat pixelFormat, YUVColorSpace colorSpace)
    : Texture(width, height, ImageOrigin::TopLeft), _pixelFormat(pixelFormat),
      _colorSpace(colorSpace) {
}

Point YUVTexture::getTextureCoord(float x, float y) const {
  return {x / static_cast<float>(width()), y / static_cast<float>(height())};
}

BackendTexture YUVTexture::getBackendTexture() const {
  return {};
}

const TextureSampler* YUVTexture::getSamplerAt(size_t index) const {
  if (index >= samplers.size()) {
    return nullptr;
  }
  return samplers[index].get();
}

size_t YUVTexture::memoryUsage() const {
  return static_cast<size_t>(width()) * static_cast<size_t>(height()) * 3 / 2;
}

void YUVTexture::onReleaseGPU() {
  for (auto& sampler : samplers) {
    context->gpu()->deleteSampler(sampler.get());
  }
}
}  // namespace tgfx
