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
std::shared_ptr<Image> PictureImage::MakeFrom(std::shared_ptr<Picture> picture, int width,
                                              int height, const Matrix* matrix) {
  if (picture == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  if (matrix && !matrix->invertible()) {
    return nullptr;
  }
  auto pictureImage = picture->asImage(width, height, matrix);
  if (pictureImage) {
    return pictureImage;
  }
  auto image = std::shared_ptr<PictureImage>(
      new PictureImage(UniqueKey::Make(), std::move(picture), width, height, matrix));
  image->weakThis = image;
  return image;
}

PictureImage::PictureImage(UniqueKey uniqueKey, std::shared_ptr<Picture> picture, int width,
                           int height, const Matrix* matrix)
    : ResourceImage(std::move(uniqueKey)), picture(std::move(picture)), _width(width),
      _height(height) {
  if (matrix && !matrix->isIdentity()) {
    this->matrix = new Matrix(*matrix);
  }
}

PictureImage::~PictureImage() {
  delete matrix;
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
  auto renderTarget = proxyProvider->createRenderTargetProxy(textureProxy, format);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto renderFlags = args.renderFlags | RenderFlags::DisableCache;
  RenderContext renderContext(renderTarget, renderFlags);
  MCState replayState(matrix ? *matrix : Matrix::I());
  picture->playback(&renderContext, replayState);
  auto drawingManager = args.context->drawingManager();
  drawingManager->addTextureResolveTask(renderTarget);
  return textureProxy;
}
}  // namespace tgfx
