/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "SubTreeCache.h"
#include "core/images/TextureImage.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {

UniqueKey SubTreeCache::makeSizeKey(int width, int height) const {
  uint32_t sizeData[2] = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  UniqueKey newKey = _uniqueKey;
  return UniqueKey::Append(newKey, sizeData, 2);
}

void SubTreeCache::addCache(Context* context, int imageWidth, int imageHeight,
                            std::shared_ptr<TextureProxy> textureProxy, const Matrix& imageMatrix) {
  if (context == nullptr || textureProxy == nullptr) {
    return;
  }
  auto sizeUniqueKey = makeSizeKey(imageWidth, imageHeight);
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, sizeUniqueKey);
  textureProxy->assignUniqueKey(sizeUniqueKey);
  _sizeMatrices[sizeUniqueKey] = imageMatrix;
}

std::optional<SubTreeCacheInfo> SubTreeCache::getSubTreeCacheInfo(Context* context, int imageWidth,
                                                                  int imageHeight) const {
  if (context == nullptr) {
    return std::nullopt;
  }
  auto sizeUniqueKey = makeSizeKey(imageWidth, imageHeight);
  auto it = _sizeMatrices.find(sizeUniqueKey);
  if (it == _sizeMatrices.end()) {
    return std::nullopt;
  }
  auto proxyProvider = context->proxyProvider();
  auto proxy = proxyProvider->findOrWrapTextureProxy(sizeUniqueKey);
  if (proxy == nullptr) {
    return std::nullopt;
  }
  auto image = TextureImage::Wrap(proxy, nullptr);
  if (image == nullptr) {
    return std::nullopt;
  }
  return SubTreeCacheInfo{std::move(image), it->second};
}
}  // namespace tgfx
