/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "PictureImage.h"
#include "core/Records.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
static bool IsEmptyPaint(const Paint* paint) {
  if (!paint) {
    return true;
  }
  if (paint->getAlpha() != 1.0f) {
    return false;
  }
  if (paint->getImageFilter() != nullptr) {
    return false;
  }
  if (paint->getColorFilter() != nullptr) {
    return false;
  }
  if (paint->getMaskFilter() != nullptr) {
    return false;
  }
  return true;
}
std::shared_ptr<Image> PictureImage::MakeFrom(std::shared_ptr<Picture> picture, int width,
                                              int height, const Matrix* matrix,
                                              const Paint* paint) {
  if (picture == nullptr || width <= 0 || height <= 0 || (paint && paint->nothingToDraw())) {
    return nullptr;
  }

  if (IsEmptyPaint(paint)) {
    auto image = picture->asImage(width, height, matrix);
    if (image) {
      return image;
    }
  }
  auto image = std::shared_ptr<PictureImage>(
      new PictureImage(UniqueKey::Make(), std::move(picture), width, height, matrix, paint));
  image->weakThis = image;
  return image;
}

PictureImage::PictureImage(UniqueKey uniqueKey, std::shared_ptr<Picture> picture, int width,
                           int height, const Matrix* matrix, const Paint* paint)
    : ResourceImage(std::move(uniqueKey)), picture(std::move(picture)), _width(width),
      _height(height) {
  if (matrix && !matrix->isIdentity()) {
    this->matrix = new Matrix(*matrix);
  }
  if (paint) {
    this->paint = new Paint(*paint);
  }
}

PictureImage::~PictureImage() {
  delete matrix;
  delete paint;
}

std::shared_ptr<TextureProxy> PictureImage::onLockTextureProxy(const TPArgs& args) const {
  auto proxyProvider = args.context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(args.uniqueKey);
  if (textureProxy != nullptr) {
    return textureProxy;
  }
  auto format = PixelFormat::RGBA_8888;
  textureProxy =
      proxyProvider->createTextureProxy(args.uniqueKey, _width, _height, format, args.mipmapped,
                                        ImageOrigin::TopLeft, args.renderFlags);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto renderTarget = proxyProvider->createRenderTargetProxy(textureProxy, format);
  SurfaceOptions surfaceOptions(args.renderFlags | RenderFlags::DisableCache);
  auto surface = Surface::MakeFrom(renderTarget, &surfaceOptions);
  if (surface == nullptr) {
    return nullptr;
  }
  auto canvas = surface->getCanvas();
  canvas->drawPicture(picture, matrix, paint);
  auto drawingManager = args.context->drawingManager();
  drawingManager->addTextureResolveTask(renderTarget);
  return textureProxy;
}
}  // namespace tgfx
