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
#include "core/Rasterizer.h"
#include "core/Records.h"
#include "gpu/DrawingManager.h"
#include "gpu/OpsCompositor.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
static bool CheckStyleAndClipForMask(const FillStyle& style, const Path& clip, int width,
                                     int height, const Matrix* matrix) {
  if (style.colorFilter || style.maskFilter || style.shader || style.color != Color::White()) {
    return false;
  }
  if (!BlendModeIsOpaque(style.blendMode, OpacityType::Opaque)) {
    return false;
  }
  if (clip.isInverseFillType()) {
    return clip.isEmpty();
  }
  Rect clipRect = {};
  if (!clip.isRect(&clipRect)) {
    return false;
  }
  if (matrix) {
    if (!matrix->rectStaysRect()) {
      return false;
    }
    matrix->mapRect(&clipRect);
  }
  return clipRect.contains(Rect::MakeWH(width, height));
}

static Matrix GetMaskMatrix(const MCState& state, const Matrix* matrix) {
  auto m = state.matrix;
  if (matrix) {
    m.postConcat(*matrix);
  }
  return m;
}

static std::shared_ptr<Rasterizer> GetEquivalentRasterizer(const Record* record, int width,
                                                           int height, const Matrix* matrix) {
  if (record->type() == RecordType::DrawShape) {
    auto shapeRecord = static_cast<const DrawShape*>(record);
    if (!CheckStyleAndClipForMask(shapeRecord->style, shapeRecord->state.clip, width, height,
                                  matrix)) {
      return nullptr;
    }
    auto shape = Shape::ApplyMatrix(shapeRecord->shape, GetMaskMatrix(shapeRecord->state, matrix));
    return Rasterizer::MakeFrom(width, height, std::move(shape), shapeRecord->style.antiAlias);
  }
  if (record->type() == RecordType::DrawGlyphRunList) {
    auto glyphRecord = static_cast<const DrawGlyphRunList*>(record);
    if (!CheckStyleAndClipForMask(glyphRecord->style, glyphRecord->state.clip, width, height,
                                  matrix)) {
      return nullptr;
    }
    return Rasterizer::MakeFrom(width, height, glyphRecord->glyphRunList,
                                glyphRecord->style.antiAlias,
                                GetMaskMatrix(glyphRecord->state, matrix));
  }
  if (record->type() == RecordType::StrokeGlyphRunList) {
    auto strokeGlyphRecord = static_cast<const StrokeGlyphRunList*>(record);
    if (!CheckStyleAndClipForMask(strokeGlyphRecord->style, strokeGlyphRecord->state.clip, width,
                                  height, matrix)) {
      return nullptr;
    }
    return Rasterizer::MakeFrom(
        width, height, strokeGlyphRecord->glyphRunList, strokeGlyphRecord->style.antiAlias,
        GetMaskMatrix(strokeGlyphRecord->state, matrix), &strokeGlyphRecord->stroke);
  }
  return nullptr;
}

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
    auto rasterizer = GetEquivalentRasterizer(picture->records[0], width, height, matrix);
    image = Image::MakeFrom(std::move(rasterizer));
    if (image) {
      return image;
    }
  }
  auto image =
      std::make_shared<PictureImage>(UniqueKey::Make(), std::move(picture), width, height, matrix);
  image->weakThis = image;
  return image;
}

PictureImage::PictureImage(UniqueKey uniqueKey, std::shared_ptr<Picture> picture, int width,
                           int height, const Matrix* matrix)
    : OffscreenImage(std::move(uniqueKey)), picture(std::move(picture)), _width(width),
      _height(height) {
  if (matrix && !matrix->isIdentity()) {
    this->matrix = new Matrix(*matrix);
  }
}

PictureImage::~PictureImage() {
  delete matrix;
}

std::unique_ptr<FragmentProcessor> PictureImage::asFragmentProcessor(
    const FPArgs& args, TileMode tileModeX, TileMode tileModeY, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  auto proxyProvider = args.context->proxyProvider();
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(uniqueKey);
  if (textureProxy != nullptr) {
    return TiledTextureEffect::Make(std::move(textureProxy), tileModeX, tileModeY, sampling,
                                    uvMatrix, isAlphaOnly());
  }

  auto drawBounds = args.drawRect;
  if (uvMatrix) {
    drawBounds = uvMatrix->mapRect(drawBounds);
  }
  auto rect = Rect::MakeWH(_width, _height);
  if (drawBounds.contains(rect)) {
    return OffscreenImage::asFragmentProcessor(args, tileModeX, tileModeY, sampling, uvMatrix);
  }
  if (!rect.intersect(drawBounds)) {
    return nullptr;
  }
  rect.roundOut();

  auto mipmapped = sampling.mipmapMode != MipmapMode::None && hasMipmaps();
  auto alphaRenderable = args.context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = isAlphaOnly() && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  textureProxy = proxyProvider->createTextureProxy(
      UniqueKey::Make(), static_cast<int>(rect.width()), static_cast<int>(rect.height()), format,
      mipmapped, ImageOrigin::TopLeft, args.renderFlags);
  auto renderTarget = proxyProvider->createRenderTargetProxy(textureProxy, format, 1);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto matrix = Matrix::MakeTrans(-rect.left, -rect.top);

  if (!drawPicture(renderTarget, args.renderFlags, &matrix)) {
    return nullptr;
  }
  if (uvMatrix) {
    matrix.preConcat(*uvMatrix);
  }
  return TiledTextureEffect::Make(renderTarget->getTextureProxy(), tileModeX, tileModeY, sampling,
                                  &matrix, isAlphaOnly());
}

bool PictureImage::onDraw(std::shared_ptr<RenderTargetProxy> renderTarget,
                          uint32_t renderFlags) const {
  return drawPicture(renderTarget, renderFlags, nullptr);
}

bool PictureImage::drawPicture(std::shared_ptr<RenderTargetProxy> renderTarget,
                               uint32_t renderFlags, Matrix* extraMatrix) const {
  if (renderTarget == nullptr) {
    return false;
  }
  RenderContext renderContext(renderTarget, renderFlags, true);
  auto totalMatrix = extraMatrix ? *extraMatrix : Matrix::I();
  if (matrix) {
    totalMatrix.preConcat(*matrix);
  }
  MCState replayState(totalMatrix);
  picture->playback(&renderContext, replayState);
  renderContext.flush();
  return true;
}

}  // namespace tgfx
