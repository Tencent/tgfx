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

#include "SubTreeCache.h"
#include "core/images/TextureImage.h"
#include "gpu/ProxyProvider.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

void SubTreeCacheDrawer::draw(Canvas* canvas, const Paint& paint,
                              const Matrix3D* transform3D) const {
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(_matrix);
  Paint drawPaint = paint;
  auto maskFilter = paint.getMaskFilter();
  if (maskFilter) {
    auto invertMatrix = Matrix::I();
    if (_matrix.invert(&invertMatrix)) {
      drawPaint.setMaskFilter(maskFilter->makeWithMatrix(invertMatrix));
    }
  }
  if (transform3D == nullptr) {
    canvas->drawImage(_image, &drawPaint);
  } else {
    auto adaptedMatrix = *transform3D;
    auto offsetMatrix =
        Matrix3D::MakeTranslate(_matrix.getTranslateX(), _matrix.getTranslateY(), 0);
    auto invOffsetMatrix =
        Matrix3D::MakeTranslate(-_matrix.getTranslateX(), -_matrix.getTranslateY(), 0);
    auto scaleMatrix = Matrix3D::MakeScale(_matrix.getScaleX(), _matrix.getScaleY(), 1.0f);
    auto invScaleMatrix =
        Matrix3D::MakeScale(1.0f / _matrix.getScaleX(), 1.0f / _matrix.getScaleY(), 1.0f);
    adaptedMatrix = invScaleMatrix * invOffsetMatrix * adaptedMatrix * offsetMatrix * scaleMatrix;
    auto imageFilter = ImageFilter::Transform3D(adaptedMatrix);
    auto offset = Point();
    auto filteredImage = _image->makeWithFilter(imageFilter, &offset);
    canvas->concat(Matrix::MakeTrans(offset.x, offset.y));
    canvas->drawImage(filteredImage, &drawPaint);
  }
  canvas->setMatrix(oldMatrix);
}

UniqueKey SubTreeCache::makeSizeKey(int longEdge) const {
  uint32_t sizeData[1] = {static_cast<uint32_t>(longEdge)};
  UniqueKey newKey = _uniqueKey;
  return UniqueKey::Append(newKey, sizeData, 1);
}

void SubTreeCache::addCache(Context* context, std::shared_ptr<TextureProxy> textureProxy,
                            const Matrix& imageMatrix) {
  if (context == nullptr || textureProxy == nullptr) {
    return;
  }
  auto sizeUniqueKey = makeSizeKey(std::max(textureProxy->width(), textureProxy->height()));
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, sizeUniqueKey);
  textureProxy->assignUniqueKey(sizeUniqueKey);
  _sizeMatrices[sizeUniqueKey] = imageMatrix;
}

std::unique_ptr<SubTreeCacheDrawer> SubTreeCache::getDrawer(Context* context, int longEdge) const {
  if (context == nullptr) {
    return nullptr;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge);
  auto it = _sizeMatrices.find(sizeUniqueKey);
  if (it == _sizeMatrices.end()) {
    return nullptr;
  }
  auto proxyProvider = context->proxyProvider();
  auto proxy = proxyProvider->findOrWrapTextureProxy(sizeUniqueKey);
  if (proxy == nullptr) {
    return nullptr;
  }
  auto image = TextureImage::Wrap(proxy, nullptr);
  if (image == nullptr) {
    return nullptr;
  }
  return std::make_unique<SubTreeCacheDrawer>(std::move(image), it->second);
}
}  // namespace tgfx
