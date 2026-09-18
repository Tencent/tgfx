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
#include <cmath>
#include "core/images/TextureImage.h"
#include "gpu/ProxyProvider.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

namespace {
// Bounds the drift between the baked contentScale and the actual contentScale of a reusable
// texture. Scale-dependent rasterization (such as hairline stroke coverage) is baked into the
// texture, so a larger drift would produce visibly shifted results when it is resampled.
// Trade-off: every drift beyond this bound re-rasterizes the whole subtree and replaces the
// bucket entry, so a monotonic zoom across a 4x range re-bakes ~28 times per cached layer. If
// zoom-time re-rasterization shows up in profiles, re-bake asynchronously while still drawing
// the stale texture instead of widening this tolerance.
constexpr float MAX_CONTENT_SCALE_DRIFT = 0.05f;
}  // namespace

UniqueKey SubtreeCache::makeSizeKey(int longEdge) const {
  uint32_t sizeData[1] = {static_cast<uint32_t>(longEdge)};
  UniqueKey newKey = _uniqueKey;
  return UniqueKey::Append(newKey, sizeData, 1);
}

void SubtreeCache::addCache(Context* context, int longEdge,
                            std::shared_ptr<TextureProxy> textureProxy, const Matrix& imageMatrix,
                            const std::shared_ptr<ColorSpace>& colorSpace, float contentScale) {
  if (context == nullptr || textureProxy == nullptr) {
    return;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge);
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, sizeUniqueKey);
  textureProxy->assignUniqueKey(sizeUniqueKey);
  cacheEntries[sizeUniqueKey] = CacheEntry{imageMatrix, colorSpace, contentScale};
}

const SubtreeCache::CacheEntry* SubtreeCache::getValidEntry(Context* context, int longEdge) const {
  if (context == nullptr) {
    return nullptr;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge);
  auto it = cacheEntries.find(sizeUniqueKey);
  if (it == cacheEntries.end()) {
    return nullptr;
  }
  auto proxyProvider = context->proxyProvider();
  if (proxyProvider->findOrWrapTextureProxy(sizeUniqueKey) == nullptr) {
    return nullptr;
  }
  return &it->second;
}

bool SubtreeCache::hasCache(Context* context, int longEdge) const {
  return getValidEntry(context, longEdge) != nullptr;
}

bool SubtreeCache::hasCache(Context* context, int longEdge, float contentScale) const {
  auto entry = getValidEntry(context, longEdge);
  if (entry == nullptr) {
    return false;
  }
  return fabsf(entry->contentScale - contentScale) <= contentScale * MAX_CONTENT_SCALE_DRIFT;
}

void SubtreeCache::draw(Context* context, int longEdge, Canvas* canvas, const Paint& paint) const {
  if (context == nullptr) {
    return;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge);
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
  canvas->drawImage(image, &drawPaint);
  canvas->setMatrix(oldMatrix);
}
}  // namespace tgfx
