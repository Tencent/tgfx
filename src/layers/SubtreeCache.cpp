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

#include "SubtreeCache.h"
#include <algorithm>
#include "core/images/TextureImage.h"
#include "gpu/ProxyProvider.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

// Encode divisor as a fixed-point integer (2 decimal places) so equal float divisors always
// produce equal keys and the SSAA flag can't be lost to fp noise. 1.0 -> 100, 2.0 -> 200.
static uint32_t EncodeScaleDivisor(float scaleDivisor) {
  auto scaled = static_cast<int>(scaleDivisor * 100.0f + 0.5f);
  return static_cast<uint32_t>(std::max(scaled, 0));
}

UniqueKey SubtreeCache::makeSizeKey(int longEdge, float scaleDivisor) const {
  uint32_t sizeData[2] = {static_cast<uint32_t>(longEdge), EncodeScaleDivisor(scaleDivisor)};
  UniqueKey newKey = _uniqueKey;
  return UniqueKey::Append(newKey, sizeData, 2);
}

void SubtreeCache::addCache(Context* context, int longEdge, float scaleDivisor,
                            std::shared_ptr<TextureProxy> textureProxy, const Matrix& imageMatrix,
                            const std::shared_ptr<ColorSpace>& colorSpace) {
  if (context == nullptr || textureProxy == nullptr) {
    return;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge, scaleDivisor);
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, sizeUniqueKey);
  textureProxy->assignUniqueKey(sizeUniqueKey);
  cacheEntries[sizeUniqueKey] = CacheEntry{imageMatrix, colorSpace};
}

bool SubtreeCache::hasCache(Context* context, int longEdge, float scaleDivisor) const {
  if (context == nullptr) {
    return false;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge, scaleDivisor);
  auto it = cacheEntries.find(sizeUniqueKey);
  if (it == cacheEntries.end()) {
    return false;
  }
  auto proxyProvider = context->proxyProvider();
  return proxyProvider->findOrWrapTextureProxy(sizeUniqueKey) != nullptr;
}

void SubtreeCache::draw(Context* context, int longEdge, float scaleDivisor, Canvas* canvas,
                        const Paint& paint, bool forceNearest) const {
  if (context == nullptr) {
    return;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge, scaleDivisor);
  auto it = cacheEntries.find(sizeUniqueKey);
  if (it == cacheEntries.end()) {
    return;
  }
  auto proxyProvider = context->proxyProvider();
  auto proxy = proxyProvider->findOrWrapTextureProxy(sizeUniqueKey);
  if (proxy == nullptr) {
    return;
  }
  auto image = TextureImage::Wrap(proxy, it->second.colorSpace);
  if (image == nullptr) {
    return;
  }
  const auto& matrix = it->second.imageMatrix;
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(matrix);
  Paint drawPaint = paint;
  auto maskFilter = paint.getMaskFilter();
  if (maskFilter) {
    auto invertMatrix = Matrix::I();
    if (matrix.invert(&invertMatrix)) {
      drawPaint.setMaskFilter(maskFilter->makeWithMatrix(invertMatrix));
    }
  }
  if (forceNearest) {
    static const SamplingOptions kNearest(FilterMode::Nearest, MipmapMode::None);
    canvas->drawImage(image, kNearest, &drawPaint);
  } else {
    canvas->drawImage(image, &drawPaint);
  }
  canvas->setMatrix(oldMatrix);
}
}  // namespace tgfx
