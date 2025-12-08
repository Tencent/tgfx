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
#include "core/images/TextureImage.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"

namespace tgfx {
std::unique_ptr<RasterizedCache> RasterizedCache::MakeFrom(Context* context, float contentScale,
                                                           std::shared_ptr<Image> image,
                                                           const Matrix& imageMatrix,
                                                           std::shared_ptr<Image>* cachedImage) {
  if (context == nullptr || image == nullptr) {
    return nullptr;
  }
  auto width = image->width();
  auto height = image->height();
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  // Lock the texture proxy from the image.
  TPArgs tpArgs(context, 0, false, 1.0f, BackingFit::Exact);
  auto textureProxy = image->lockTextureProxy(tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto cache = std::make_unique<RasterizedCache>(context->uniqueID(), contentScale, imageMatrix,
                                                 image->colorSpace());
  // Assign unique key to the texture proxy for caching.
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, cache->uniqueKey());
  textureProxy->assignUniqueKey(cache->uniqueKey());
  // Return the cached image so the caller can use it for immediate drawing.
  if (cachedImage != nullptr) {
    *cachedImage = TextureImage::Wrap(std::move(textureProxy), image->colorSpace());
  }
  return cache;
}

bool RasterizedCache::valid(Context* context) const {
  if (context == nullptr || context->uniqueID() != _contextID || _uniqueKey.empty()) {
    return false;
  }
  auto proxyProvider = context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(_uniqueKey);
  return textureProxy != nullptr;
}

void RasterizedCache::draw(Context* context, Canvas* canvas, bool antiAlias, float alpha,
                           const std::shared_ptr<MaskFilter>& mask, BlendMode blendMode,
                           const Matrix3D* transform) const {
  if (context == nullptr || canvas == nullptr) {
    return;
  }
  if (context->uniqueID() != _contextID) {
    return;
  }
  auto proxyProvider = context->proxyProvider();
  auto proxy = proxyProvider->findOrWrapTextureProxy(_uniqueKey);
  if (proxy == nullptr) {
    return;
  }
  auto image = TextureImage::Wrap(proxy, _colorSpace);
  if (image == nullptr) {
    return;
  }
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(matrix);
  Paint paint = {};
  paint.setAntiAlias(antiAlias);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  if (mask) {
    auto invertMatrix = Matrix::I();
    if (matrix.invert(&invertMatrix)) {
      paint.setMaskFilter(mask->makeWithMatrix(invertMatrix));
    }
  }
  if (transform == nullptr) {
    canvas->drawImage(image, &paint);
  } else {
    // Transform describes a transformation based on the layer's coordinate system, but the
    // rasterized content is only a small sub-rectangle within the layer. We need to calculate an
    // equivalent affine transformation matrix referenced to the local coordinate system with the
    // top-left vertex of this sub-rectangle as the origin.
    auto adaptedMatrix = *transform;
    auto offsetMatrix = Matrix3D::MakeTranslate(matrix.getTranslateX(), matrix.getTranslateY(), 0);
    auto invOffsetMatrix =
        Matrix3D::MakeTranslate(-matrix.getTranslateX(), -matrix.getTranslateY(), 0);
    auto scaleMatrix = Matrix3D::MakeScale(matrix.getScaleX(), matrix.getScaleY(), 1.0f);
    auto invScaleMatrix =
        Matrix3D::MakeScale(1.0f / matrix.getScaleX(), 1.0f / matrix.getScaleY(), 1.0f);
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
