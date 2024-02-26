/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Canvas.h"
#include <atomic>
#include "CanvasState.h"
#include "core/PathRef.h"
#include "core/Rasterizer.h"
#include "gpu/PathAATriangles.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/ClearOp.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/ops/RRectOp.h"
#include "gpu/ops/TriangulatingPathOp.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Mask.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/gpu/Surface.h"
#include "tgfx/utils/UTF.h"
#include "utils/MathExtra.h"
#include "utils/SimpleTextShaper.h"

namespace tgfx {
// https://chromium-review.googlesource.com/c/chromium/src/+/1099564/
static constexpr int AA_TESSELLATOR_MAX_VERB_COUNT = 100;
// A factor used to estimate the memory size of a tessellated path, based on the average value of
// Buffer.size() / Path.countPoints() from 4300+ tessellated path data.
static constexpr int AA_TESSELLATOR_BUFFER_SIZE_FACTOR = 170;

static constexpr uint32_t FirstUnreservedClipID = 1;

static uint32_t NextClipID() {
  static std::atomic<uint32_t> nextID{FirstUnreservedClipID};
  uint32_t id;
  do {
    id = nextID.fetch_add(1, std::memory_order_relaxed);
  } while (id < FirstUnreservedClipID);
  return id;
}

Canvas::Canvas(Surface* surface) : surface(surface), clipID(kDefaultClipID) {
  state = std::make_shared<CanvasState>();
  state->clip.addRect(0, 0, static_cast<float>(surface->width()),
                      static_cast<float>(surface->height()));
  state->clipID = NextClipID();
}

void Canvas::save() {
  auto canvasState = std::make_shared<CanvasState>();
  *canvasState = *state;
  savedStateList.push_back(canvasState);
}

void Canvas::restore() {
  if (savedStateList.empty()) {
    return;
  }
  state = savedStateList.back();
  savedStateList.pop_back();
}

void Canvas::translate(float dx, float dy) {
  state->matrix.preTranslate(dx, dy);
}

void Canvas::scale(float sx, float sy) {
  state->matrix.preScale(sx, sy);
}

void Canvas::rotate(float degrees) {
  state->matrix.preRotate(degrees);
}

void Canvas::rotate(float degress, float px, float py) {
  state->matrix.preRotate(degress, px, py);
}

void Canvas::skew(float sx, float sy) {
  state->matrix.preSkew(sx, sy);
}

void Canvas::concat(const Matrix& matrix) {
  state->matrix.preConcat(matrix);
}

Matrix Canvas::getMatrix() const {
  return state->matrix;
}

void Canvas::setMatrix(const Matrix& matrix) {
  state->matrix = matrix;
}

void Canvas::resetMatrix() {
  state->matrix.reset();
}

Path Canvas::getTotalClip() const {
  return state->clip;
}

void Canvas::clipRect(const tgfx::Rect& rect) {
  Path path = {};
  path.addRect(rect);
  clipPath(path);
}

void Canvas::clipPath(const Path& path) {
  auto clipPath = path;
  clipPath.transform(state->matrix);
  state->clip.addPath(clipPath, PathOp::Intersect);
  state->clipID = NextClipID();
}

void Canvas::clear(const Color& color) {
  Paint paint;
  paint.setColor(color);
  paint.setBlendMode(BlendMode::Src);
  auto rect = Rect::MakeWH(surface->width(), surface->height());
  drawRect(rect, paint);
}

void Canvas::drawLine(float x0, float y0, float x1, float y1, const Paint& paint) {
  Path path = {};
  path.moveTo(x0, y0);
  path.lineTo(x1, y1);
  auto realPaint = paint;
  realPaint.setStyle(PaintStyle::Stroke);
  drawPath(path, realPaint);
}

void Canvas::drawRect(const Rect& rect, const Paint& paint) {
  Path path = {};
  path.addRect(rect);
  drawPath(path, paint);
}

void Canvas::drawOval(const Rect& oval, const Paint& paint) {
  Path path = {};
  path.addOval(oval);
  drawPath(path, paint);
}

void Canvas::drawCircle(float centerX, float centerY, float radius, const Paint& paint) {
  Rect rect =
      Rect::MakeLTRB(centerX - radius, centerY - radius, centerX + radius, centerY + radius);
  drawOval(rect, paint);
}

static Paint CleanPaintForDrawImage(const Paint* paint) {
  Paint cleaned;
  if (paint) {
    cleaned = *paint;
    cleaned.setStyle(PaintStyle::Fill);
  }
  return cleaned;
}

Context* Canvas::getContext() const {
  return surface->getContext();
}

Surface* Canvas::getSurface() const {
  return surface;
}

const SurfaceOptions* Canvas::surfaceOptions() const {
  return surface->options();
}

std::shared_ptr<TextureProxy> Canvas::getClipTexture() {
  if (clipID != state->clipID) {
    _clipSurface = nullptr;
  }
  if (_clipSurface == nullptr) {
    _clipSurface = Surface::Make(getContext(), surface->width(), surface->height(), true);
    if (_clipSurface == nullptr) {
      _clipSurface = Surface::Make(getContext(), surface->width(), surface->height());
    }
  }
  if (_clipSurface == nullptr) {
    return nullptr;
  }
  if (clipID != state->clipID) {
    auto clipCanvas = _clipSurface->getCanvas();
    clipCanvas->clear();
    Paint paint = {};
    paint.setColor(Color::White());
    clipCanvas->drawPath(state->clip, paint);
    clipID = state->clipID;
  }
  return _clipSurface->getTextureProxy();
}

static constexpr float BOUNDS_TO_LERANCE = 1e-3f;

/**
 * Returns true if the given rect counts as aligned with pixel boundaries.
 */
static bool IsPixelAligned(const Rect& rect) {
  return fabsf(roundf(rect.left) - rect.left) <= BOUNDS_TO_LERANCE &&
         fabsf(roundf(rect.top) - rect.top) <= BOUNDS_TO_LERANCE &&
         fabsf(roundf(rect.right) - rect.right) <= BOUNDS_TO_LERANCE &&
         fabsf(roundf(rect.bottom) - rect.bottom) <= BOUNDS_TO_LERANCE;
}

void FlipYIfNeeded(Rect* rect, const Surface* surface) {
  if (surface->origin() == ImageOrigin::BottomLeft) {
    auto height = rect->height();
    rect->top = static_cast<float>(surface->height()) - rect->bottom;
    rect->bottom = rect->top + height;
  }
}

std::pair<std::optional<Rect>, bool> Canvas::getClipRect(const Rect* drawBounds) {
  auto rect = Rect::MakeEmpty();
  if (state->clip.asRect(&rect)) {
    if (drawBounds != nullptr && !rect.intersect(*drawBounds)) {
      return {{}, false};
    }
    FlipYIfNeeded(&rect, surface);
    if (IsPixelAligned(rect)) {
      rect.round();
      if (rect != Rect::MakeWH(surface->width(), surface->height())) {
        return {rect, true};
      } else {
        return {Rect::MakeEmpty(), false};
      }
    } else {
      return {rect, false};
    }
  }
  return {{}, false};
}

std::unique_ptr<FragmentProcessor> Canvas::getClipMask(const Rect& deviceBounds,
                                                       Rect* scissorRect) {
  const auto& clipPath = state->clip;
  if (clipPath.contains(deviceBounds)) {
    return nullptr;
  }
  auto [rect, useScissor] = getClipRect();
  if (rect.has_value()) {
    if (!rect->isEmpty()) {
      *scissorRect = *rect;
      if (!useScissor) {
        scissorRect->roundOut();
        return AARectEffect::Make(*rect);
      }
    }
    return nullptr;
  }
  auto clipBounds = clipPath.getBounds();
  FlipYIfNeeded(&clipBounds, surface);
  clipBounds.roundOut();
  *scissorRect = clipBounds;
  return FragmentProcessor::MulInputByChildAlpha(
      DeviceSpaceTextureEffect::Make(getClipTexture(), surface->origin()));
}

Rect Canvas::clipLocalBounds(Rect localBounds) {
  auto deviceBounds = state->matrix.mapRect(localBounds);
  auto clipBounds = state->clip.getBounds();
  clipBounds.roundOut();
  auto clippedDeviceBounds = deviceBounds;
  if (!clippedDeviceBounds.intersect(clipBounds)) {
    return Rect::MakeEmpty();
  }
  auto clippedLocalBounds = localBounds;
  if (state->matrix.getSkewX() == 0 && state->matrix.getSkewY() == 0 &&
      clippedDeviceBounds != deviceBounds) {
    Matrix inverse = Matrix::I();
    state->matrix.invert(&inverse);
    clippedLocalBounds = inverse.mapRect(clippedDeviceBounds);
    clippedLocalBounds.intersect(localBounds);
  }
  return clippedLocalBounds;
}

static std::unique_ptr<DrawOp> MakeSimplePathOp(const Path& path, const DrawArgs& args) {
  Rect rect = {};
  if (path.asRect(&rect)) {
    return FillRectOp::Make(args.color, rect, args.viewMatrix);
  }
  RRect rRect;
  if (path.asRRect(&rRect)) {
    return RRectOp::Make(args.color, rRect, args.viewMatrix);
  }
  return nullptr;
}

static std::unique_ptr<DrawOp> MakeTriangulatingPathOp(const Path& path, const DrawArgs& args,
                                                       const Point& scales, const Stroke* stroke) {
  static const auto TriangulatingPathType = UniqueID::Next();
  BytesKey bytesKey = {};
  Matrix rasterizeMatrix = {};
  if (scales.x == scales.y) {
    rasterizeMatrix.setScale(scales.x, scales.y);
    bytesKey.reserve(2);
    bytesKey.write(TriangulatingPathType);
    bytesKey.write(scales.x);
  } else {
    rasterizeMatrix = args.viewMatrix;
    rasterizeMatrix.setTranslateX(0);
    rasterizeMatrix.setTranslateY(0);
    bytesKey.reserve(5);
    bytesKey.write(TriangulatingPathType);
    bytesKey.write(rasterizeMatrix.getScaleX());
    bytesKey.write(rasterizeMatrix.getSkewX());
    bytesKey.write(rasterizeMatrix.getSkewY());
    bytesKey.write(rasterizeMatrix.getScaleY());
  }
  auto uniqueKey = UniqueKey::Combine(PathRef::GetUniqueKey(path), bytesKey);
  auto pathTriangles = PathAATriangles::Make(path, rasterizeMatrix, stroke);
  auto proxyProvider = args.context->proxyProvider();
  auto bufferProxy = proxyProvider->createGpuBufferProxy(uniqueKey, pathTriangles,
                                                         BufferType::Vertex, args.renderFlags);
  if (bufferProxy == nullptr) {
    return nullptr;
  }
  auto viewMatrix = args.viewMatrix;
  auto drawBounds = viewMatrix.mapRect(args.drawRect);
  Matrix localMatrix = {};
  if (!rasterizeMatrix.invert(&localMatrix)) {
    return nullptr;
  }
  viewMatrix.preConcat(localMatrix);
  return TriangulatingPathOp::Make(args.color, std::move(bufferProxy), drawBounds, viewMatrix,
                                   localMatrix);
}

static std::unique_ptr<DrawOp> MakeTexturePathOp(const Path& path, const DrawArgs& args,
                                                 const Point& scales, const Rect& bounds,
                                                 const Stroke* stroke) {
  static const auto TexturePathType = UniqueID::Next();
  BytesKey bytesKey(3);
  bytesKey.write(TexturePathType);
  bytesKey.write(scales.x);
  bytesKey.write(scales.y);
  auto uniqueKey = UniqueKey::Combine(PathRef::GetUniqueKey(path), bytesKey);
  auto width = ceilf(bounds.width());
  auto height = ceilf(bounds.height());
  auto localMatrix = Matrix::MakeScale(scales.x, scales.y);
  localMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto rasterizer = Rasterizer::MakeFrom(path, ISize::Make(width, height), localMatrix, stroke);
  auto proxyProvider = args.context->proxyProvider();
  auto textureProxy =
      proxyProvider->createTextureProxy(uniqueKey, rasterizer, false, args.renderFlags);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto maskProcessor =
      TextureEffect::Make(std::move(textureProxy), SamplingOptions(), &localMatrix);
  if (maskProcessor == nullptr) {
    return nullptr;
  }
  auto op = FillRectOp::Make(args.color, args.drawRect, args.viewMatrix);
  op->addColorFP(std::move(maskProcessor));
  return op;
}

static Path GetSimpleFillPath(const Path& path, const Paint& paint) {
  if (paint.getStyle() == PaintStyle::Fill) {
    return path;
  }
  if (path.isLine()) {
    auto effect = PathEffect::MakeStroke(paint.getStroke());
    if (effect != nullptr) {
      auto tempPath = path;
      effect->applyTo(&tempPath);
      return tempPath;
    }
  }
  return {};
}

void Canvas::drawPath(const Path& path, const Paint& paint) {
  if (path.isEmpty() || paint.nothingToDraw()) {
    return;
  }
  auto stroke = paint.getStyle() == PaintStyle::Stroke ? paint.getStroke() : nullptr;
  auto pathBounds = path.getBounds();
  if (stroke != nullptr) {
    pathBounds.outset(stroke->width, stroke->width);
  }
  auto localBounds = clipLocalBounds(pathBounds);
  if (localBounds.isEmpty()) {
    return;
  }
  auto fillPath = GetSimpleFillPath(path, paint);
  if (drawAsClear(fillPath, paint)) {
    return;
  }
  auto inputColor = paint.getColor().premultiply();
  DrawArgs args(getContext(), surface->options()->renderFlags(), inputColor, localBounds,
                state->matrix);
  auto drawOp = MakeSimplePathOp(fillPath, args);
  if (drawOp != nullptr) {
    addDrawOp(std::move(drawOp), args, paint);
    return;
  }
  auto scales = state->matrix.getAxisScales();
  if (FloatNearlyZero(scales.x) || FloatNearlyZero(scales.y)) {
    return;
  }
  auto scaledBounds = pathBounds;
  scaledBounds.scale(scales.x, scales.y);
  auto width = static_cast<int>(ceilf(scaledBounds.width()));
  auto height = static_cast<int>(ceilf(scaledBounds.height()));
  if (path.countVerbs() <= AA_TESSELLATOR_MAX_VERB_COUNT ||
      width * height >= path.countPoints() * AA_TESSELLATOR_BUFFER_SIZE_FACTOR) {
    drawOp = MakeTriangulatingPathOp(path, args, scales, stroke);
  } else {
    drawOp = MakeTexturePathOp(path, args, scales, scaledBounds, stroke);
  }
  addDrawOp(std::move(drawOp), args, paint);
}

void Canvas::drawImage(std::shared_ptr<Image> image, float left, float top, const Paint* paint) {
  drawImage(image, Matrix::MakeTrans(left, top), paint);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const Matrix& matrix, const Paint* paint) {
  auto oldMatrix = getMatrix();
  concat(matrix);
  drawImage(std::move(image), paint);
  setMatrix(oldMatrix);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const Paint* paint) {
  if (image == nullptr) {
    return;
  }
  auto mipmapMode = image->hasMipmaps() ? MipmapMode::Linear : MipmapMode::None;
  SamplingOptions sampling(FilterMode::Linear, mipmapMode);
  drawImage(std::move(image), sampling, paint);
}

void Canvas::drawImage(std::shared_ptr<Image> image, SamplingOptions sampling, const Paint* paint) {
  if (image == nullptr) {
    return;
  }
  auto realPaint = CleanPaintForDrawImage(paint);
  if (realPaint.nothingToDraw()) {
    return;
  }
  auto oldMatrix = getMatrix();
  auto imageFilter = realPaint.getImageFilter();
  if (imageFilter != nullptr) {
    auto offset = Point::Zero();
    image = image->makeWithFilter(std::move(imageFilter), &offset);
    if (image == nullptr) {
      return;
    }
    realPaint.setImageFilter(nullptr);
    concat(Matrix::MakeTrans(offset.x, offset.y));
  }
  auto localBounds = clipLocalBounds(Rect::MakeWH(image->width(), image->height()));
  if (localBounds.isEmpty()) {
    return;
  }
  if (realPaint.getShader() != nullptr && !image->isAlphaOnly()) {
    realPaint.setShader(nullptr);
  }
  auto inputColor = realPaint.getColor().premultiply();
  DrawArgs args(getContext(), surface->options()->renderFlags(), inputColor, localBounds,
                state->matrix, sampling);
  auto processor = FragmentProcessor::Make(std::move(image), args);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = FillRectOp::Make(args.color, args.drawRect, args.viewMatrix);
  drawOp->addColorFP(std::move(processor));
  addDrawOp(std::move(drawOp), args, realPaint, true);
  setMatrix(oldMatrix);
}

void Canvas::drawMask(const Rect& deviceBounds, std::shared_ptr<TextureProxy> textureProxy,
                      const Paint& paint) {
  if (textureProxy == nullptr) {
    return;
  }
  auto localMatrix = Matrix::I();
  auto maskLocalMatrix = Matrix::I();
  if (!state->matrix.invert(&localMatrix)) {
    return;
  }
  maskLocalMatrix.postConcat(state->matrix);
  maskLocalMatrix.postTranslate(-deviceBounds.x(), -deviceBounds.y());
  maskLocalMatrix.postScale(static_cast<float>(textureProxy->width()) / deviceBounds.width(),
                            static_cast<float>(textureProxy->height()) / deviceBounds.height());
  auto oldMatrix = state->matrix;
  resetMatrix();
  auto inputColor = paint.getColor().premultiply();
  DrawArgs args(getContext(), surface->options()->renderFlags(), inputColor, deviceBounds,
                Matrix::I());
  auto op = FillRectOp::Make(args.color, args.drawRect, args.viewMatrix, &localMatrix);
  auto maskProcessor = FragmentProcessor::MulInputByChildAlpha(
      TextureEffect::Make(std::move(textureProxy), SamplingOptions(), &maskLocalMatrix));
  if (maskProcessor == nullptr) {
    return;
  }
  op->addMaskFP(std::move(maskProcessor));
  addDrawOp(std::move(op), args, paint);
  setMatrix(oldMatrix);
}

void Canvas::drawSimpleText(const std::string& text, float x, float y, const tgfx::Font& font,
                            const tgfx::Paint& paint) {
  auto [glyphIDs, positions] = SimpleTextShaper::Shape(text, font);
  if (x != 0 && y != 0) {
    for (auto& position : positions) {
      position.offset(x, y);
    }
  }
  drawGlyphs(glyphIDs.data(), positions.data(), glyphIDs.size(), font, paint);
}

void Canvas::drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                        const Font& font, const Paint& paint) {
  if (glyphCount == 0 || paint.nothingToDraw()) {
    return;
  }
  auto scale = state->matrix.getMaxScale();
  auto scaledFont = font.makeWithSize(font.getSize() * scale);
  auto scaledPaint = paint;
  scaledPaint.setStrokeWidth(paint.getStrokeWidth() * scale);
  std::vector<Point> scaledPositions;
  for (size_t i = 0; i < glyphCount; ++i) {
    scaledPositions.push_back(Point::Make(positions[i].x * scale, positions[i].y * scale));
  }
  save();
  concat(Matrix::MakeScale(1.f / scale));
  if (scaledFont.getTypeface()->hasColor()) {
    drawColorGlyphs(glyphs, scaledPositions.data(), glyphCount, scaledFont, scaledPaint);
    restore();
    return;
  }
  auto textBlob = TextBlob::MakeFrom(glyphs, scaledPositions.data(), glyphCount, scaledFont);
  if (textBlob) {
    drawMaskGlyphs(textBlob, scaledPaint);
  }
  restore();
}

void Canvas::drawColorGlyphs(const GlyphID glyphIDs[], const Point positions[], size_t glyphCount,
                             const Font& font, const Paint& paint) {
  for (size_t i = 0; i < glyphCount; ++i) {
    const auto& glyphID = glyphIDs[i];
    const auto& position = positions[i];

    auto glyphMatrix = Matrix::I();
    auto glyphBuffer = font.getImage(glyphID, &glyphMatrix);
    if (glyphBuffer == nullptr) {
      continue;
    }
    glyphMatrix.postTranslate(position.x, position.y);
    save();
    concat(glyphMatrix);
    auto image = Image::MakeFrom(glyphBuffer);
    drawImage(std::move(image), &paint);
    restore();
  }
}

void Canvas::drawMaskGlyphs(std::shared_ptr<TextBlob> textBlob, const Paint& paint) {
  if (textBlob == nullptr) {
    return;
  }
  auto stroke = paint.getStyle() == PaintStyle::Stroke ? paint.getStroke() : nullptr;
  auto localBounds = clipLocalBounds(textBlob->getBounds(stroke));
  if (localBounds.isEmpty()) {
    return;
  }
  auto deviceBounds = state->matrix.mapRect(localBounds);
  auto width = ceilf(deviceBounds.width());
  auto height = ceilf(deviceBounds.height());
  auto totalMatrix = state->matrix;
  auto matrix = Matrix::I();
  matrix.postTranslate(-deviceBounds.x(), -deviceBounds.y());
  matrix.postScale(width / deviceBounds.width(), height / deviceBounds.height());
  totalMatrix.postConcat(matrix);
  auto rasterizer = Rasterizer::MakeFrom(textBlob, ISize::Make(width, height), totalMatrix, stroke);
  auto textureProxy = getContext()->proxyProvider()->createTextureProxy(
      {}, std::move(rasterizer), false, surface->options()->renderFlags());
  drawMask(deviceBounds, std::move(textureProxy), paint);
}

void Canvas::drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                       const Color colors[], size_t count, SamplingOptions sampling,
                       const Paint* paint) {
  // TODO: Support blend mode, atlas as source, colors as destination, colors can be nullptr.
  if (atlas == nullptr || count == 0) {
    return;
  }
  auto totalMatrix = getMatrix();
  std::vector<std::unique_ptr<FillRectOp>> ops;
  FillRectOp* op = nullptr;
  Rect drawRect = Rect::MakeEmpty();
  for (size_t i = 0; i < count; ++i) {
    concat(matrix[i]);
    auto width = static_cast<float>(tex[i].width());
    auto height = static_cast<float>(tex[i].height());
    auto localBounds = clipLocalBounds(Rect::MakeWH(width, height));
    if (localBounds.isEmpty()) {
      setMatrix(totalMatrix);
      continue;
    }
    drawRect.join(localBounds);
    auto localMatrix = Matrix::MakeTrans(tex[i].x(), tex[i].y());
    auto color = colors ? std::optional<Color>(colors[i].premultiply()) : std::nullopt;
    if (op == nullptr) {
      ops.emplace_back(FillRectOp::Make(color, localBounds, state->matrix, &localMatrix));
      op = ops.back().get();
    } else {
      if (!op->add(color, localBounds, state->matrix, &localMatrix)) {
        ops.emplace_back(FillRectOp::Make(color, localBounds, state->matrix, &localMatrix));
        op = ops.back().get();
      }
    }
    setMatrix(totalMatrix);
  }
  if (ops.empty()) {
    return;
  }
  auto realPaint = CleanPaintForDrawImage(paint);
  auto inputColor = realPaint.getColor().premultiply();
  DrawArgs args(getContext(), surface->options()->renderFlags(), inputColor, drawRect,
                state->matrix, sampling);
  for (auto& rectOp : ops) {
    auto processor = FragmentProcessor::Make(atlas, args);
    if (colors) {
      processor = FragmentProcessor::MulInputByChildAlpha(std::move(processor));
    }
    if (processor == nullptr) {
      return;
    }
    rectOp->addColorFP(std::move(processor));
    addDrawOp(std::move(rectOp), args, realPaint);
  }
}

static bool HasColorOnly(const Paint& paint) {
  return paint.getColorFilter() == nullptr && paint.getShader() == nullptr &&
         paint.getImageFilter() == nullptr && paint.getMaskFilter() == nullptr;
}

bool Canvas::drawAsClear(const Path& path, const Paint& paint) {
  if (!HasColorOnly(paint) || !state->matrix.rectStaysRect()) {
    return false;
  }
  auto color = paint.getColor().premultiply();
  auto blendMode = paint.getBlendMode();
  if (blendMode == BlendMode::Clear) {
    color = Color::Transparent();
  } else if (blendMode != BlendMode::Src) {
    if (!color.isOpaque()) {
      return false;
    }
  }
  auto bounds = Rect::MakeEmpty();
  if (!path.asRect(&bounds)) {
    return false;
  }
  state->matrix.mapRect(&bounds);
  auto [clipRect, useScissor] = getClipRect(&bounds);
  if (clipRect.has_value()) {
    auto format = surface->renderTargetProxy->format();
    const auto& writeSwizzle = getContext()->caps()->getWriteSwizzle(format);
    color = writeSwizzle.applyTo(color);
    if (useScissor) {
      surface->aboutToDraw();
      surface->addOp(ClearOp::Make(color, *clipRect));
      return true;
    } else if (clipRect->isEmpty()) {
      surface->aboutToDraw(true);
      surface->addOp(ClearOp::Make(color, bounds));
      return true;
    }
  }
  return false;
}

bool Canvas::getProcessors(const DrawArgs& args, const Paint& paint, DrawOp* drawOp) {
  if (drawOp == nullptr) {
    return false;
  }
  if (auto shader = paint.getShader()) {
    auto shaderFP = FragmentProcessor::Make(shader, args);
    if (shaderFP == nullptr) {
      return false;
    }
    drawOp->addColorFP(std::move(shaderFP));
  }
  if (auto colorFilter = paint.getColorFilter()) {
    if (auto processor = colorFilter->asFragmentProcessor()) {
      drawOp->addColorFP(std::move(processor));
    } else {
      return false;
    }
  }
  if (auto maskFilter = paint.getMaskFilter()) {
    if (auto processor = maskFilter->asFragmentProcessor(args, nullptr)) {
      drawOp->addMaskFP(std::move(processor));
    } else {
      return false;
    }
  }
  return true;
}

void Canvas::addDrawOp(std::unique_ptr<DrawOp> op, const DrawArgs& args, const Paint& paint,
                       bool aa) {
  if (!getProcessors(args, paint, op.get())) {
    return;
  }
  auto aaType = AAType::None;
  if (surface->renderTargetProxy->sampleCount() > 1) {
    aaType = AAType::MSAA;
  } else if (aa && !IsPixelAligned(op->bounds())) {
    aaType = AAType::Coverage;
  } else {
    const auto& matrix = state->matrix;
    auto rotation = std::round(RadiansToDegrees(atan2f(matrix.getSkewX(), matrix.getScaleX())));
    if (static_cast<int>(rotation) % 90 != 0) {
      aaType = AAType::Coverage;
    }
  }
  Rect scissorRect = Rect::MakeEmpty();
  auto clipMask = getClipMask(op->bounds(), &scissorRect);
  if (clipMask) {
    op->addMaskFP(std::move(clipMask));
  }
  op->setScissorRect(scissorRect);
  op->setBlendMode(paint.getBlendMode());
  op->setAA(aaType);
  surface->aboutToDraw(false);
  surface->addOp(std::move(op));
}
}  // namespace tgfx
