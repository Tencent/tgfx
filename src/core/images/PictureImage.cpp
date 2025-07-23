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

#include "PictureImage.h"
#include "ScaleImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/OpsCompositor.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/TPArgs.h"
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
  if (picture->drawCount == 1) {
    ISize clipSize = {width, height};
    // PictureImage is not a ResourceImage because it can be very large, while ResourceImage always
    // caches the full image by default. With PictureImage, usually only a portion is needed,
    // especially for image filters. So, we only unwrap the image inside the picture and avoid
    // creating a ResourceImage for paths or text.
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
                           const Matrix* matrix, bool mipmapped, float scale)
    : picture(std::move(picture)), _width(width), _height(height), mipmapped(mipmapped),
      _scale(scale) {
  if (matrix && !matrix->isIdentity()) {
    this->matrix = new Matrix(*matrix);
  }
}

PictureImage::~PictureImage() {
  delete matrix;
}

int PictureImage::width() const {
  return ScaleImage::GetScaledSize(_width, _scale);
}

int PictureImage::height() const {
  return ScaleImage::GetScaledSize(_height, _scale);
}

std::shared_ptr<Image> PictureImage::onMakeMipmapped(bool enabled) const {
  if (enabled == mipmapped) {
    return weakThis.lock();
  }
  auto newImage = std::make_shared<PictureImage>(picture, _width, _height, matrix, enabled, _scale);
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<Image> PictureImage::onMakeScaled(float scale, const SamplingOptions&) const {
  auto newImage =
      std::make_shared<PictureImage>(picture, _width, _height, matrix, mipmapped, scale * _scale);
  newImage->weakThis = newImage;
  return newImage;
}

PlacementPtr<FragmentProcessor> PictureImage::asFragmentProcessor(const FPArgs& args,
                                                                  const SamplingArgs& samplingArgs,
                                                                  const Matrix* uvMatrix) const {
  auto drawBounds = args.drawRect;
  if (uvMatrix) {
    drawBounds = uvMatrix->mapRect(drawBounds);
  }
  auto rect = Rect::MakeWH(width(), height());
  if (!rect.intersect(drawBounds)) {
    return nullptr;
  }
  rect.roundOut();
  auto mipmapped = samplingArgs.sampling.mipmapMode != MipmapMode::None && hasMipmaps();
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(rect.width()), static_cast<int>(rect.height()), isAlphaOnly(),
      1, mipmapped, ImageOrigin::TopLeft, BackingFit::Approx);

  if (renderTarget == nullptr) {
    return nullptr;
  }
  Point offset = Point::Make(-rect.left, -rect.top);
  if (!drawPicture(renderTarget, args.renderFlags, &offset)) {
    return nullptr;
  }
  auto finalUVMatrix = Matrix::MakeTrans(-rect.left, -rect.top);
  if (uvMatrix) {
    finalUVMatrix.preConcat(*uvMatrix);
  }
  auto newSamplingArgs = samplingArgs;
  if (samplingArgs.sampleArea) {
    newSamplingArgs.sampleArea = samplingArgs.sampleArea->makeOffset(-rect.left, -rect.top);
  }
  return TiledTextureEffect::Make(renderTarget->asTextureProxy(), newSamplingArgs, &finalUVMatrix,
                                  isAlphaOnly());
}

std::shared_ptr<TextureProxy> PictureImage::lockTextureProxy(const TPArgs& args) const {
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, width(), height(), isAlphaOnly(), 1, hasMipmaps() && args.mipmapped,
      ImageOrigin::TopLeft, BackingFit::Approx);
  if (renderTarget == nullptr) {
    return nullptr;
  }

  if (!drawPicture(renderTarget, args.renderFlags, nullptr)) {
    return nullptr;
  }
  return renderTarget->asTextureProxy();
}

bool PictureImage::drawPicture(std::shared_ptr<RenderTargetProxy> renderTarget,
                               uint32_t renderFlags, const Point* offset) const {
  if (renderTarget == nullptr) {
    return false;
  }
  RenderContext renderContext(std::move(renderTarget), renderFlags, true);
  Matrix totalMatrix =
      Matrix::MakeScale(static_cast<float>(width()) / static_cast<float>(_width),
                        static_cast<float>(height()) / static_cast<float>(_height));
  if (offset) {
    totalMatrix.postTranslate(offset->x, offset->y);
  }
  if (matrix) {
    totalMatrix.preConcat(*matrix);
  }
  MCState replayState(totalMatrix);
  picture->playback(&renderContext, replayState);
  renderContext.flush();
  return true;
}

}  // namespace tgfx
