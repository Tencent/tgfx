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

#include "GradientCache.h"
#include "core/SIMDVec.h"
#include "tgfx/core/Pixmap.h"
#ifdef _MSC_VER
#include "core/SIMDHighwayInterface.h"
#endif

namespace tgfx {
// Each bitmap will be 256x1.
static constexpr size_t MaxNumCachedGradientBitmaps = 32;
static constexpr int GradientTextureSize = 256;

std::shared_ptr<Texture> GradientCache::find(const BytesKey& bytesKey) {
  auto iter = textures.find(bytesKey);
  if (iter == textures.end()) {
    return nullptr;
  }
  keys.remove(bytesKey);
  keys.push_front(bytesKey);
  return iter->second;
}

void GradientCache::add(const BytesKey& bytesKey, std::shared_ptr<Texture> texture) {
  textures[bytesKey] = std::move(texture);
  keys.push_front(bytesKey);
  while (keys.size() > MaxNumCachedGradientBitmaps) {
    auto key = keys.back();
    keys.pop_back();
    textures.erase(key);
  }
}

std::shared_ptr<ImageBuffer> CreateGradient(const Color* colors, const float* positions, int count,
                                            int resolution) {
#ifdef _MSC_VER
  return CreateGradientHWY(colors, positions, count, resolution);
#else
  Bitmap bitmap(resolution, 1, false, false);
  Pixmap pixmap(bitmap);
  if (pixmap.isEmpty()) {
    return nullptr;
  }
  pixmap.clear();
  auto pixels = reinterpret_cast<uint8_t*>(pixmap.writablePixels());
  int prevIndex = 0;
  for (int i = 1; i < count; ++i) {
    int nextIndex =
        std::min(static_cast<int>(positions[i] * static_cast<float>(resolution)), resolution - 1);

    if (nextIndex > prevIndex) {
      float4 color0 = float4::Load(&colors[i - 1].red);
      float4 color1 = float4::Load(&colors[i].red);

      auto step = 1.0f / static_cast<float>(nextIndex - prevIndex);
      float4 delta = (color1 - color0) * step;

      for (int curIndex = prevIndex; curIndex <= nextIndex; ++curIndex) {
        byte4 res = Cast<uint8_t, 4, float>(color0 * 255.0f);
        res.store(&pixels[curIndex * 4]);
        color0 += delta;
      }
    }
    prevIndex = nextIndex;
  }
  return bitmap.makeBuffer();
#endif
}

std::shared_ptr<Texture> GradientCache::getGradient(Context* context, const Color* colors,
                                                    const float* positions, int count) {
  BytesKey bytesKey = {};
  for (int i = 0; i < count; ++i) {
    bytesKey.write(colors[i].red);
    bytesKey.write(colors[i].green);
    bytesKey.write(colors[i].blue);
    bytesKey.write(colors[i].alpha);
    bytesKey.write(positions[i]);
  }

  auto texture = find(bytesKey);
  if (texture) {
    return texture;
  }
  auto pixelBuffer = CreateGradient(colors, positions, count, GradientTextureSize);
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  texture = Texture::MakeFrom(context, pixelBuffer);
  if (texture) {
    add(bytesKey, texture);
  }
  return texture;
}

void GradientCache::releaseAll() {
  textures.clear();
  keys.clear();
}

bool GradientCache::empty() const {
  return textures.empty() && keys.empty();
}
}  // namespace tgfx
