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
#include "gpu/DrawingManager.h"
#include "gpu/OpsCompositor.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
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
                           const Matrix* matrix, bool mipmapped)
    : picture(std::move(picture)), _width(width), _height(height), mipmapped(mipmapped),
      _colorSpace(ColorSpace::MakeSRGB()) {
  if (matrix && !matrix->isIdentity()) {
    this->matrix = new Matrix(*matrix);
  }
}

PictureImage::~PictureImage() {
  delete matrix;
}

std::shared_ptr<Image> PictureImage::onMakeScaled(int newWidth, int newHeight,
                                                  const SamplingOptions&) const {
  auto newMatrix = matrix != nullptr ? *matrix : Matrix::I();
  newMatrix.postScale(static_cast<float>(newWidth) / static_cast<float>(_width),
                      static_cast<float>(newHeight) / static_cast<float>(_height));
  auto newImage =
      std::make_shared<PictureImage>(picture, newWidth, newHeight, &newMatrix, mipmapped);
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<Image> PictureImage::onMakeMipmapped(bool enabled) const {
  auto newImage = std::make_shared<PictureImage>(picture, _width, _height, matrix, enabled);
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
  auto clipRect = rect;
  rect.scale(args.drawScale, args.drawScale);
  rect.round();
  // recalculate the scale factor to avoid the precision loss of floating point numbers
  auto scaleX = rect.width() / clipRect.width();
  auto scaleY = rect.height() / clipRect.height();
  auto mipmapped = samplingArgs.sampling.mipmapMode != MipmapMode::None && hasMipmaps();
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, static_cast<int>(rect.width()), static_cast<int>(rect.height()), isAlphaOnly(),
      1, mipmapped, ImageOrigin::TopLeft, BackingFit::Approx);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto extraMatrix = Matrix::MakeScale(scaleX, scaleY);
  extraMatrix.preTranslate(-clipRect.left, -clipRect.top);
  if (!drawPicture(renderTarget, args.renderFlags, &extraMatrix)) {
    return nullptr;
  }
  auto finalUVMatrix = extraMatrix;
  if (uvMatrix) {
    finalUVMatrix.preConcat(*uvMatrix);
  }
  auto newSamplingArgs = samplingArgs;
  if (samplingArgs.sampleArea) {
    newSamplingArgs.sampleArea = extraMatrix.mapRect(*samplingArgs.sampleArea);
  }
  return TiledTextureEffect::Make(renderTarget->asTextureProxy(), newSamplingArgs, &finalUVMatrix,
                                  isAlphaOnly());
}

std::shared_ptr<TextureProxy> PictureImage::lockTextureProxy(const TPArgs& args) const {
  auto textureWidth = _width;
  auto textureHeight = _height;
  if (args.drawScale < 1.0f) {
    textureWidth = static_cast<int>(roundf(static_cast<float>(_width) * args.drawScale));
    textureHeight = static_cast<int>(roundf(static_cast<float>(_height) * args.drawScale));
  }
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, textureWidth, textureHeight, isAlphaOnly(), 1, hasMipmaps() && args.mipmapped,
      ImageOrigin::TopLeft, args.backingFit);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto matrix = Matrix::MakeScale(static_cast<float>(textureWidth) / static_cast<float>(_width),
                                  static_cast<float>(textureHeight) / static_cast<float>(_height));
  if (!drawPicture(renderTarget, args.renderFlags, &matrix)) {
    return nullptr;
  }
  return renderTarget->asTextureProxy();
}

bool PictureImage::drawPicture(std::shared_ptr<RenderTargetProxy> renderTarget,
                               uint32_t renderFlags, const Matrix* extraMatrix) const {
  if (renderTarget == nullptr) {
    return false;
  }
  RenderContext renderContext(std::move(renderTarget), renderFlags, true, nullptr, _colorSpace);
  Matrix totalMatrix = {};
  if (extraMatrix) {
    totalMatrix = *extraMatrix;
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
