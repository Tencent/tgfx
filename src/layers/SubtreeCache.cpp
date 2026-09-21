/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "SubtreeCache.h"
#include <cmath>
#include "core/images/TextureImage.h"
#include "gpu/ProxyProvider.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

namespace {
// Bounds the number of coexisting density entries; the least recently added one is evicted
// when exceeded. The backing textures are additionally bounded by the proxy provider's
// resource budget, so evicting an entry only drops its lookup metadata early.
constexpr size_t MAX_CACHE_ENTRIES = 16;
}  // namespace

uint32_t SubtreeCache::QuantizeContentScale(float contentScale) {
  if (!(contentScale > 0.0f)) {
    return 0;
  }
  return static_cast<uint32_t>(std::lround(contentScale * 20.0f));
}

UniqueKey SubtreeCache::makeScaleKey(int longEdge, uint32_t scaleIndex) const {
  uint32_t keyData[2] = {static_cast<uint32_t>(longEdge), scaleIndex};
  UniqueKey newKey = _uniqueKey;
  return UniqueKey::Append(newKey, keyData, 2);
}

void SubtreeCache::addCache(Context* context, int longEdge, float contentScale,
                            std::shared_ptr<TextureProxy> textureProxy, const Matrix& imageMatrix,
                            const std::shared_ptr<ColorSpace>& colorSpace) {
  if (context == nullptr || textureProxy == nullptr) {
    return;
  }
  auto scaleIndex = QuantizeContentScale(contentScale);
  auto scaleUniqueKey = makeScaleKey(longEdge, scaleIndex);
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, scaleUniqueKey);
  textureProxy->assignUniqueKey(scaleUniqueKey);
  cacheEntries[scaleUniqueKey] = CacheEntry{imageMatrix, colorSpace, ++_stampCounter};
  while (cacheEntries.size() > MAX_CACHE_ENTRIES) {
    auto oldest = cacheEntries.begin();
    for (auto it = cacheEntries.begin(); it != cacheEntries.end(); ++it) {
      if (it->second.stamp < oldest->second.stamp) {
        oldest = it;
      }
    }
    cacheEntries.erase(oldest);
  }
}

bool SubtreeCache::hasCache(Context* context, int longEdge, float contentScale) const {
  if (context == nullptr) {
    return false;
  }
  auto scaleUniqueKey = makeScaleKey(longEdge, QuantizeContentScale(contentScale));
  auto it = cacheEntries.find(scaleUniqueKey);
  if (it == cacheEntries.end()) {
    return false;
  }
  auto proxyProvider = context->proxyProvider();
  return proxyProvider->findOrWrapTextureProxy(scaleUniqueKey) != nullptr;
}

void SubtreeCache::draw(Context* context, int longEdge, float contentScale, Canvas* canvas,
                        const Paint& paint) const {
  if (context == nullptr) {
    return;
  }
  auto scaleUniqueKey = makeScaleKey(longEdge, QuantizeContentScale(contentScale));
  auto it = cacheEntries.find(scaleUniqueKey);
  if (it == cacheEntries.end()) {
    return;
  }
  auto proxyProvider = context->proxyProvider();
  auto proxy = proxyProvider->findOrWrapTextureProxy(scaleUniqueKey);
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
  canvas->drawImage(image, &drawPaint);
  canvas->setMatrix(oldMatrix);
}
}  // namespace tgfx
