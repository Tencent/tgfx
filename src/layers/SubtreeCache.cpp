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
#include "core/images/TextureImage.h"
#include "core/utils/MathExtra.h"
#include "gpu/ProxyProvider.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

/**
 * Calculates the transformation matrix to be applied when drawing the image.
 * @param contextMatrix The transformation matrix to be applied to the drawing environment.
 * @param imageMatrix The transformation matrix of the image relative to the environment.
 */
static inline Matrix3D AdaptedImageMatrix(const Matrix3D& contextMatrix,
                                          const Matrix& imageMatrix) {
  // ContextMatrix describes a transformation based on the layer's coordinate system, but the
  // rasterized content is only a small sub-rectangle within the layer. We need to calculate an
  // equivalent affine transformation matrix referenced to the local coordinate system with the
  // top-left vertex of this sub-rectangle as the origin.
  auto adaptedMatrix = contextMatrix;
  auto offsetMatrix =
      Matrix3D::MakeTranslate(imageMatrix.getTranslateX(), imageMatrix.getTranslateY(), 0);
  auto invOffsetMatrix =
      Matrix3D::MakeTranslate(-imageMatrix.getTranslateX(), -imageMatrix.getTranslateY(), 0);
  auto scaleMatrix = Matrix3D::MakeScale(imageMatrix.getScaleX(), imageMatrix.getScaleY(), 1.0f);
  auto invScaleMatrix =
      Matrix3D::MakeScale(1.0f / imageMatrix.getScaleX(), 1.0f / imageMatrix.getScaleY(), 1.0f);
  return invScaleMatrix * invOffsetMatrix * adaptedMatrix * offsetMatrix * scaleMatrix;
}

UniqueKey SubtreeCache::makeSizeKey(int longEdge) const {
  uint32_t sizeData[1] = {static_cast<uint32_t>(longEdge)};
  UniqueKey newKey = _uniqueKey;
  return UniqueKey::Append(newKey, sizeData, 1);
}

void SubtreeCache::addCache(Context* context, int longEdge,
                            std::shared_ptr<TextureProxy> textureProxy, const Matrix& imageMatrix,
                            const std::shared_ptr<ColorSpace>& colorSpace) {
  if (context == nullptr || textureProxy == nullptr) {
    return;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge);
  auto proxyProvider = context->proxyProvider();
  proxyProvider->assignProxyUniqueKey(textureProxy, sizeUniqueKey);
  textureProxy->assignUniqueKey(sizeUniqueKey);
  cacheEntries[sizeUniqueKey] = CacheEntry{imageMatrix, colorSpace};
}

bool SubtreeCache::hasCache(Context* context, int longEdge) const {
  if (context == nullptr) {
    return false;
  }
  auto sizeUniqueKey = makeSizeKey(longEdge);
  auto it = cacheEntries.find(sizeUniqueKey);
  if (it == cacheEntries.end()) {
    return false;
  }
  auto proxyProvider = context->proxyProvider();
  return proxyProvider->findOrWrapTextureProxy(sizeUniqueKey) != nullptr;
}

void SubtreeCache::draw(Context* context, int longEdge, Canvas* canvas, const Paint& paint,
                        const Matrix3D* transform3D) const {
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
  if (transform3D == nullptr) {
    canvas->drawImage(image, &drawPaint);
  } else {
    auto adaptedMatrix = AdaptedImageMatrix(*transform3D, matrix);
    // Layer visibility is handled in the CPU stage, update the matrix to keep the Z-axis of
    // vertices sent to the GPU at 0.
    adaptedMatrix.setRow(2, {0, 0, 0, 0});
    auto imageFilter = ImageFilter::Transform3D(adaptedMatrix);
    auto offset = Point();
    auto filteredImage = image->makeWithFilter(imageFilter, &offset);
    canvas->concat(Matrix::MakeTrans(offset.x, offset.y));
    canvas->drawImage(filteredImage, &drawPaint);
  }
  canvas->setMatrix(oldMatrix);
}

}  // namespace tgfx
