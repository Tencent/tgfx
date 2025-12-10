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

#include "RasterizedCache.h"
#include <cmath>
#include "core/images/TextureImage.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"

namespace tgfx {
inline uint32_t ScaleToKey(float scale) {
  if (scale <= 0.0f) {
    return 0;
  }
  return static_cast<uint32_t>(std::roundf(scale * 1000.0f));
}

UniqueKey RasterizedCache::MakeScaleKey(float scale) const {
  auto scaleKey = ScaleToKey(scale);
  UniqueKey newKey = _uniqueKey;
  return UniqueKey::Append(newKey, &scaleKey, 1);
}

std::unique_ptr<RasterizedCache> RasterizedCache::MakeFrom(Context* context) {
  if (context == nullptr) {
    return nullptr;
  }
  return std::make_unique<RasterizedCache>(context->uniqueID());
}

std::shared_ptr<Image> RasterizedCache::addScaleCache(Context* context, float contentScale,
                                                      std::shared_ptr<Image> image,
                                                      const Matrix& imageMatrix) {
  if (context == nullptr || image == nullptr || context->uniqueID() != _contextID) {
    return nullptr;
  }
  auto width = image->width();
  auto height = image->height();
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  TPArgs tpArgs(context, 0, false, 1.0f, BackingFit::Exact);
  auto textureProxy = image->lockTextureProxy(tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }

  auto scaleUniqueKey = MakeScaleKey(contentScale);
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, scaleUniqueKey);
  textureProxy->assignUniqueKey(scaleUniqueKey);

  _scaleMatrices[scaleUniqueKey] = imageMatrix;

  return TextureImage::Wrap(std::move(textureProxy), image->colorSpace());
}

bool RasterizedCache::valid(Context* context, float scale) {
  if (context == nullptr || context->uniqueID() != _contextID) {
    return false;
  }
  auto scaleUniqueKey = MakeScaleKey(scale);
  if (_scaleMatrices.find(scaleUniqueKey) == _scaleMatrices.end()) {
    return false;
  }
  auto proxyProvider = context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(scaleUniqueKey);
  auto isValid = textureProxy != nullptr;
  if (!isValid) {
    _scaleMatrices.erase(scaleUniqueKey);
  }
  return isValid;
}

void RasterizedCache::draw(Context* context, Canvas* canvas, float cacheScale, bool antiAlias,
                           float alpha, const std::shared_ptr<MaskFilter>& mask,
                           BlendMode blendMode, const Matrix3D* transform) const {
  if (context == nullptr || canvas == nullptr) {
    return;
  }
  if (context->uniqueID() != _contextID) {
    return;
  }

  auto scaleUniqueKey = MakeScaleKey(cacheScale);
  auto it = _scaleMatrices.find(scaleUniqueKey);
  if (it == _scaleMatrices.end()) {
    return;
  }
  auto matrixPtr = &it->second;

  auto proxyProvider = context->proxyProvider();
  auto proxy = proxyProvider->findOrWrapTextureProxy(scaleUniqueKey);
  if (proxy == nullptr) {
    return;
  }
  auto image = TextureImage::Wrap(proxy, nullptr);
  if (image == nullptr) {
    return;
  }
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(*matrixPtr);
  Paint paint = {};
  paint.setAntiAlias(antiAlias);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  if (mask) {
    auto invertMatrix = Matrix::I();
    if (matrixPtr->invert(&invertMatrix)) {
      paint.setMaskFilter(mask->makeWithMatrix(invertMatrix));
    }
  }
  if (transform == nullptr) {
    canvas->drawImage(image, &paint);
  } else {
    auto adaptedMatrix = *transform;
    auto offsetMatrix =
        Matrix3D::MakeTranslate(matrixPtr->getTranslateX(), matrixPtr->getTranslateY(), 0);
    auto invOffsetMatrix =
        Matrix3D::MakeTranslate(-matrixPtr->getTranslateX(), -matrixPtr->getTranslateY(), 0);
    auto scaleMatrix = Matrix3D::MakeScale(matrixPtr->getScaleX(), matrixPtr->getScaleY(), 1.0f);
    auto invScaleMatrix =
        Matrix3D::MakeScale(1.0f / matrixPtr->getScaleX(), 1.0f / matrixPtr->getScaleY(), 1.0f);
    adaptedMatrix = invScaleMatrix * invOffsetMatrix * adaptedMatrix * offsetMatrix * scaleMatrix;
    auto imageFilter = ImageFilter::Transform3D(adaptedMatrix);
    auto offset = Point();
    auto filteredImage = image->makeWithFilter(imageFilter, &offset);
    canvas->concat(Matrix::MakeTrans(offset.x, offset.y));
    canvas->drawImage(filteredImage, &paint);
  }
  canvas->setMatrix(oldMatrix);
}
}  // namespace tgfx
