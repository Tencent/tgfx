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
#include "gpu/DrawingManager.h"
#include "gpu/OpsCompositor.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<Picture> picture, int width, int height,
                                       const Matrix* matrix) {
  if (picture == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  if (matrix && !matrix->invertible()) {
    return nullptr;
  }
  if (picture->records.size() == 1) {
    ISize clipSize = {width, height};
    auto image = picture->asImage(nullptr, matrix, &clipSize);
    if (image) {
      return image;
    }
  }
  auto image = std::make_shared<PictureImage>(std::move(picture), width, height, matrix);
  image->weakThis = image;
  return image;
}

PictureImage::PictureImage(std::shared_ptr<Picture> picture, int width, int height,
                           const Matrix* matrix, bool mipmapped)
    : picture(std::move(picture)), _width(width), _height(height), mipmapped(mipmapped) {
  if (matrix && !matrix->isIdentity()) {
    this->matrix = new Matrix(*matrix);
  }
}

PictureImage::~PictureImage() {
  delete matrix;
}

std::shared_ptr<Image> PictureImage::onMakeMipmapped(bool enabled) const {
  return std::make_shared<PictureImage>(picture, _width, _height, matrix, enabled);
}

std::unique_ptr<FragmentProcessor> PictureImage::asFragmentProcessor(
    const FPArgs& args, TileMode tileModeX, TileMode tileModeY, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {

  auto drawBounds = args.drawRect;
  if (uvMatrix) {
    drawBounds = uvMatrix->mapRect(drawBounds);
  }
  auto rect = Rect::MakeWH(_width, _height);
  if (!rect.intersect(drawBounds)) {
    return nullptr;
  }
  rect.roundOut();

  auto mipmapped = sampling.mipmapMode != MipmapMode::None && hasMipmaps();
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, width(), height(), isAlphaOnly() && alphaRenderable, 1, mipmapped);

  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto matrix = Matrix::MakeTrans(-rect.left, -rect.top);
  if (!drawPicture(renderTarget, args.renderFlags, matrix)) {
    return nullptr;
  }
  if (uvMatrix) {
    matrix.preConcat(*uvMatrix);
  }
  return TiledTextureEffect::Make(renderTarget->getTextureProxy(), tileModeX, tileModeY, sampling,
                                  &matrix, isAlphaOnly());
}

bool PictureImage::drawPicture(std::shared_ptr<RenderTargetProxy> renderTarget,
                               uint32_t renderFlags, const Matrix& extraMatrix) const {
  if (renderTarget == nullptr) {
    return false;
  }
  RenderContext renderContext(renderTarget, renderFlags, true);
  auto totalMatrix = extraMatrix;
  if (matrix) {
    totalMatrix.preConcat(*matrix);
  }
  MCState replayState(totalMatrix);
  picture->playback(&renderContext, replayState);
  renderContext.flush();
  return true;
}

}  // namespace tgfx
