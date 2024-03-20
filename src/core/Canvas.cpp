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
#include "core/FillStyle.h"
#include "core/MCStack.h"
#include "core/PathRef.h"
#include "core/Rasterizer.h"
#include "core/SimpleTextBlob.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/ClearOp.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/ops/RRectOp.h"
#include "gpu/ops/TriangulatingPathOp.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/gpu/Surface.h"
#include "utils/SimpleTextShaper.h"
#include "utils/StrokeKey.h"

namespace tgfx {
// https://chromium-review.googlesource.com/c/chromium/src/+/1099564/
static constexpr int AA_TESSELLATOR_MAX_VERB_COUNT = 100;
// A factor used to estimate the memory size of a tessellated path, based on the average value of
// Buffer.size() / Path.countPoints() from 4300+ tessellated path data.
static constexpr int AA_TESSELLATOR_BUFFER_SIZE_FACTOR = 170;
/**
 * Defines the maximum distance a draw can extend beyond a clip's boundary and still be considered
 * 'on the other side'. This tolerance accounts for potential floating point rounding errors. The
 * value of 1e-3 is chosen because, in the coverage case, as long as coverage stays within
 * 0.5 * 1/256 of its intended value, it shouldn't affect the final pixel values.
 */
static constexpr float BOUNDS_TOLERANCE = 1e-3f;

enum class SrcColorOpacity {
  Unknown,
  // The src color is known to be opaque (alpha == 255)
  Opaque,
  // The src color is known to be fully transparent (color == 0)
  TransparentBlack,
  // The src alpha is known to be fully transparent (alpha == 0)
  TransparentAlpha,
};

Canvas::Canvas(Surface* surface) : surface(surface) {
  Path clip = {};
  clip.addRect(0, 0, static_cast<float>(surface->width()), static_cast<float>(surface->height()));
  mcStack = new MCStack(clip);
}

Canvas::~Canvas() {
  delete mcStack;
}

void Canvas::save() {
  mcStack->save();
}

void Canvas::restore() {
  mcStack->restore();
}

void Canvas::translate(float dx, float dy) {
  mcStack->translate(dx, dy);
}

void Canvas::scale(float sx, float sy) {
  mcStack->scale(sx, sy);
}

void Canvas::rotate(float degrees) {
  mcStack->rotate(degrees);
}

void Canvas::rotate(float degress, float px, float py) {
  mcStack->rotate(degress, px, py);
}

void Canvas::skew(float sx, float sy) {
  mcStack->skew(sx, sy);
}

void Canvas::concat(const Matrix& matrix) {
  mcStack->concat(matrix);
}

Matrix Canvas::getMatrix() const {
  return mcStack->getMatrix();
}

void Canvas::setMatrix(const Matrix& matrix) {
  mcStack->setMatrix(matrix);
}

void Canvas::resetMatrix() {
  mcStack->resetMatrix();
}

Path Canvas::getTotalClip() const {
  return mcStack->getClip();
}

void Canvas::clipRect(const tgfx::Rect& rect) {
  mcStack->clipRect(rect);
}

void Canvas::clipPath(const Path& path) {
  mcStack->clipPath(path);
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

Context* Canvas::getContext() const {
  return surface->getContext();
}

Surface* Canvas::getSurface() const {
  return surface;
}

const SurfaceOptions* Canvas::surfaceOptions() const {
  return surface->options();
}

static FillStyle CreateFillStyle(const Paint& paint) {
  FillStyle style = {};
  auto shader = paint.getShader();
  Color color = {};
  if (shader && shader->asColor(&color)) {
    color.alpha *= paint.getAlpha();
    style.color = color.premultiply();
    shader = nullptr;
  } else {
    style.color = paint.getColor().premultiply();
  }
  style.shader = shader;
  style.antiAlias = paint.isAntiAlias();
  style.colorFilter = paint.getColorFilter();
  style.maskFilter = paint.getMaskFilter();
  style.blendMode = paint.getBlendMode();
  return style;
}

static FillStyle CreateFillStyle(std::shared_ptr<Image> image, SamplingOptions sampling,
                                 const Paint* paint) {
  auto isAlphaOnly = image->isAlphaOnly();
  auto shader =
      Shader::MakeImageShader(std::move(image), TileMode::Clamp, TileMode::Clamp, sampling);
  if (!paint) {
    FillStyle style = {};
    style.shader = shader;
    return style;
  }
  auto style = CreateFillStyle(*paint);
  if (isAlphaOnly && style.shader) {
    style.shader = Shader::MakeBlend(BlendMode::DstIn, std::move(shader), std::move(style.shader));
  } else {
    style.shader = shader;
  }
  return style;
}

static bool ShouldTriangulatePath(const Path& path, const Matrix& viewMatrix) {
  if (path.countVerbs() <= AA_TESSELLATOR_MAX_VERB_COUNT) {
    return true;
  }
  auto scales = viewMatrix.getAxisScales();
  auto bounds = path.getBounds();
  bounds.scale(scales.x, scales.y);
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  return path.countPoints() * AA_TESSELLATOR_BUFFER_SIZE_FACTOR <= width * height;
}

DrawArgs Canvas::makeDrawArgs(const Rect& localBounds, const Matrix& viewMatrix) {
  Matrix invert = {};
  if (!viewMatrix.invert(&invert)) {
    return {};
  }
  auto drawRect = localBounds;
  auto clipBounds = mcStack->getClip().getBounds();
  invert.mapRect(&clipBounds);
  if (!drawRect.intersect(clipBounds)) {
    return {};
  }
  return {getContext(), surfaceOptions()->renderFlags(), drawRect, viewMatrix};
}

void Canvas::drawPath(const Path& path, const Paint& paint) {
  if (path.isEmpty() || paint.nothingToDraw()) {
    return;
  }
  auto stroke = paint.getStroke();
  auto style = CreateFillStyle(paint);
  if (stroke && path.isLine()) {
    auto effect = PathEffect::MakeStroke(stroke);
    if (effect != nullptr) {
      auto fillPath = path;
      effect->applyTo(&fillPath);
      if (drawSimplePath(fillPath, style)) {
        return;
      }
    }
  }
  if (!stroke && drawSimplePath(path, style)) {
    return;
  }
  auto pathBounds = path.getBounds();
  if (stroke != nullptr) {
    pathBounds.outset(stroke->width, stroke->width);
  }
  auto args = makeDrawArgs(pathBounds, mcStack->getMatrix());
  if (args.empty()) {
    return;
  }
  std::unique_ptr<DrawOp> drawOp = nullptr;
  if (ShouldTriangulatePath(path, args.viewMatrix)) {
    drawOp =
        TriangulatingPathOp::Make(style.color, path, args.viewMatrix, stroke, args.renderFlags);
  } else {
    auto maskFP = makeTextureMask(path, args.viewMatrix, stroke);
    if (maskFP != nullptr) {
      drawOp = FillRectOp::Make(style.color, args.drawRect, args.viewMatrix);
      drawOp->addCoverageFP(std::move(maskFP));
    }
  }
  addDrawOp(std::move(drawOp), args, style);
}

static std::unique_ptr<FragmentProcessor> CreateMaskFP(std::shared_ptr<TextureProxy> textureProxy,
                                                       const Matrix* localMatrix = nullptr) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto isAlphaOnly = textureProxy->isAlphaOnly();
  auto processor = TextureEffect::Make(std::move(textureProxy), {}, localMatrix);
  if (processor == nullptr) {
    return nullptr;
  }
  if (!isAlphaOnly) {
    processor = FragmentProcessor::MulInputByChildAlpha(std::move(processor));
  }
  return processor;
}

std::unique_ptr<FragmentProcessor> Canvas::makeTextureMask(const Path& path,
                                                           const Matrix& viewMatrix,
                                                           const Stroke* stroke) {
  auto scales = viewMatrix.getAxisScales();
  auto bounds = path.getBounds();
  bounds.scale(scales.x, scales.y);
  static const auto TexturePathType = UniqueID::Next();
  BytesKey bytesKey(3 + (stroke ? StrokeKeyCount : 0));
  bytesKey.write(TexturePathType);
  bytesKey.write(scales.x);
  bytesKey.write(scales.y);
  if (stroke) {
    WriteStrokeKey(&bytesKey, stroke);
  }
  auto uniqueKey = UniqueKey::Combine(PathRef::GetUniqueKey(path), bytesKey);
  auto width = ceilf(bounds.width());
  auto height = ceilf(bounds.height());
  auto rasterizeMatrix = Matrix::MakeScale(scales.x, scales.y);
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto rasterizer = Rasterizer::MakeFrom(path, ISize::Make(width, height), rasterizeMatrix, stroke);
  auto proxyProvider = getContext()->proxyProvider();
  auto textureProxy = proxyProvider->createTextureProxy(uniqueKey, rasterizer, false,
                                                        surfaceOptions()->renderFlags());
  return CreateMaskFP(std::move(textureProxy), &rasterizeMatrix);
}

bool Canvas::drawSimplePath(const Path& path, const FillStyle& style) {
  Rect rect = {};
  if (path.asRect(&rect)) {
    drawRect(rect, mcStack->getMatrix(), style);
    return true;
  }
  RRect rRect;
  if (path.asRRect(&rRect)) {
    auto args = makeDrawArgs(rRect.rect, mcStack->getMatrix());
    auto drawOp = RRectOp::Make(style.color, rRect, args.viewMatrix);
    addDrawOp(std::move(drawOp), args, style);
    return true;
  }
  return false;
}

void Canvas::drawRect(const Rect& rect, const Matrix& viewMatrix, const FillStyle& style) {
  if (drawAsClear(rect, viewMatrix, style)) {
    return;
  }
  auto args = makeDrawArgs(rect, viewMatrix);
  if (args.empty()) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, args.drawRect, args.viewMatrix);
  addDrawOp(std::move(drawOp), args, style);
}

static bool HasColorOnly(const FillStyle& style) {
  return style.colorFilter == nullptr && style.shader == nullptr && style.maskFilter == nullptr;
}

bool Canvas::drawAsClear(const Rect& rect, const Matrix& viewMatrix, const FillStyle& style) {
  if (!HasColorOnly(style) || !viewMatrix.rectStaysRect()) {
    return false;
  }
  auto color = style.color;
  if (style.blendMode == BlendMode::Clear) {
    color = Color::Transparent();
  } else if (style.blendMode != BlendMode::Src) {
    if (!color.isOpaque()) {
      return false;
    }
  }
  auto bounds = rect;
  viewMatrix.mapRect(&bounds);
  auto [clipRect, useScissor] = getClipRect(&bounds);
  if (clipRect.has_value()) {
    auto format = surface->renderTargetProxy->format();
    const auto& writeSwizzle = getContext()->caps()->getWriteSwizzle(format);
    color = writeSwizzle.applyTo(color);
    if (useScissor) {
      surface->addOp(ClearOp::Make(color, *clipRect));
      return true;
    } else if (clipRect->isEmpty()) {
      surface->addOp(ClearOp::Make(color, bounds), true);
      return true;
    }
  }
  return false;
}

static SamplingOptions GetDefaultSamplingOptions(std::shared_ptr<Image> image) {
  if (image == nullptr) {
    return {};
  }
  auto mipmapMode = image->hasMipmaps() ? MipmapMode::Linear : MipmapMode::None;
  return SamplingOptions(FilterMode::Linear, mipmapMode);
}

void Canvas::drawImage(std::shared_ptr<Image> image, float left, float top, const Paint* paint) {
  drawImage(std::move(image), Matrix::MakeTrans(left, top), paint);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const Matrix& matrix, const Paint* paint) {
  auto sampling = GetDefaultSamplingOptions(image);
  drawImage(std::move(image), sampling, paint, &matrix);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const Paint* paint) {
  auto sampling = GetDefaultSamplingOptions(image);
  drawImage(std::move(image), sampling, paint, nullptr);
}

void Canvas::drawImage(std::shared_ptr<Image> image, SamplingOptions sampling, const Paint* paint) {
  drawImage(std::move(image), sampling, paint, nullptr);
}

void Canvas::drawImage(std::shared_ptr<Image> image, SamplingOptions sampling, const Paint* paint,
                       const Matrix* extraMatrix) {
  if (image == nullptr || (paint && paint->nothingToDraw())) {
    return;
  }
  auto viewMatrix = mcStack->getMatrix();
  if (extraMatrix) {
    viewMatrix.preConcat(*extraMatrix);
  }
  auto imageFilter = paint ? paint->getImageFilter() : nullptr;
  if (imageFilter != nullptr) {
    auto offset = Point::Zero();
    image = image->makeWithFilter(std::move(imageFilter), &offset);
    if (image == nullptr) {
      return;
    }
    viewMatrix.preTranslate(offset.x, offset.y);
  }
  auto rect = Rect::MakeWH(image->width(), image->height());
  auto style = CreateFillStyle(std::move(image), sampling, paint);
  drawRect(rect, viewMatrix, style);
}

void Canvas::drawSimpleText(const std::string& text, float x, float y, const tgfx::Font& font,
                            const tgfx::Paint& paint) {
  if (text.empty() || paint.nothingToDraw()) {
    return;
  }
  auto glyphRun = SimpleTextShaper::Shape(text, font);
  auto viewMatrix = mcStack->getMatrix();
  viewMatrix.preTranslate(x, y);
  auto style = CreateFillStyle(paint);
  drawGlyphs(std::move(glyphRun), viewMatrix, style, paint.getStroke());
}

void Canvas::drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                        const Font& font, const Paint& paint) {
  if (glyphCount == 0 || paint.nothingToDraw()) {
    return;
  }
  GlyphRun glyphRun(font, {glyphs, glyphs + glyphCount}, {positions, positions + glyphCount});
  auto style = CreateFillStyle(paint);
  drawGlyphs(std::move(glyphRun), mcStack->getMatrix(), style, paint.getStroke());
}

void Canvas::drawGlyphs(GlyphRun glyphRun, const Matrix& viewMatrix, const FillStyle& style,
                        const Stroke* stroke) {
  if (glyphRun.empty()) {
    return;
  }
  if (glyphRun.hasColor()) {
    drawColorGlyphs(glyphRun, viewMatrix, style);
    return;
  }
  auto maxScale = viewMatrix.getMaxScale();
  // Scale the glyphs before measuring to prevent precision loss with small font sizes.
  auto bounds = glyphRun.getBounds(maxScale, stroke);
  auto localBounds = bounds;
  localBounds.scale(1.0f / maxScale, 1.0f / maxScale);
  auto args = makeDrawArgs(localBounds, viewMatrix);
  if (args.empty()) {
    return;
  }
  auto rasterizeMatrix = Matrix::MakeScale(maxScale, maxScale);
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto textBlob = std::make_shared<SimpleTextBlob>(std::move(glyphRun));
  auto rasterizer = Rasterizer::MakeFrom(std::move(textBlob), ISize::Make(width, height),
                                         rasterizeMatrix, stroke);
  auto proxyProvider = getContext()->proxyProvider();
  auto textureProxy = proxyProvider->createTextureProxy({}, rasterizer, false, args.renderFlags);
  auto processor = CreateMaskFP(std::move(textureProxy), &rasterizeMatrix);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, args.drawRect, viewMatrix);
  drawOp->addCoverageFP(std::move(processor));
  addDrawOp(std::move(drawOp), args, style);
}

void Canvas::drawColorGlyphs(const GlyphRun& glyphRun, const Matrix& viewMatrix,
                             const FillStyle& style) {
  auto scale = viewMatrix.getMaxScale();
  auto drawMatrix = viewMatrix;
  drawMatrix.preScale(1.0f / scale, 1.0f / scale);
  auto font = glyphRun.font();
  font = font.makeWithSize(font.getSize() * scale);
  auto glyphCount = glyphRun.runSize();
  auto& glyphIDs = glyphRun.glyphIDs();
  auto& positions = glyphRun.positions();
  for (size_t i = 0; i < glyphCount; ++i) {
    const auto& glyphID = glyphIDs[i];
    const auto& position = positions[i];
    auto glyphMatrix = Matrix::I();
    auto glyphImage = font.getImage(glyphID, &glyphMatrix);
    if (glyphImage == nullptr) {
      continue;
    }
    glyphMatrix.postTranslate(position.x * scale, position.y * scale);
    glyphMatrix.postConcat(drawMatrix);
    auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
    auto glyphStyle = style;
    glyphStyle.shader = Shader::MakeImageShader(std::move(glyphImage));
    drawRect(rect, glyphMatrix, glyphStyle);
  }
}

void Canvas::drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                       const Color colors[], size_t count, SamplingOptions sampling,
                       const Paint* paint) {
  // TODO: Support blend mode, atlas as source, colors as destination, colors can be nullptr.
  if (atlas == nullptr || count == 0 || (paint && paint->nothingToDraw())) {
    return;
  }
  auto style = CreateFillStyle(std::move(atlas), sampling, paint);
  for (size_t i = 0; i < count; ++i) {
    auto rect = tex[i];
    auto viewMatrix = mcStack->getMatrix();
    viewMatrix.preConcat(matrix[i]);
    viewMatrix.preTranslate(-rect.x(), -rect.y());
    auto glyphStyle = style;
    if (colors) {
      glyphStyle.color = colors[i].premultiply();
    }
    drawRect(rect, viewMatrix, glyphStyle);
  }
}

/**
 * Returns true if the given rect counts as aligned with pixel boundaries.
 */
static bool IsPixelAligned(const Rect& rect) {
  return fabsf(roundf(rect.left) - rect.left) <= BOUNDS_TOLERANCE &&
         fabsf(roundf(rect.top) - rect.top) <= BOUNDS_TOLERANCE &&
         fabsf(roundf(rect.right) - rect.right) <= BOUNDS_TOLERANCE &&
         fabsf(roundf(rect.bottom) - rect.bottom) <= BOUNDS_TOLERANCE;
}

static void FlipYIfNeeded(Rect* rect, const Surface* surface) {
  if (surface->origin() == ImageOrigin::BottomLeft) {
    auto height = rect->height();
    rect->top = static_cast<float>(surface->height()) - rect->bottom;
    rect->bottom = rect->top + height;
  }
}

std::pair<std::optional<Rect>, bool> Canvas::getClipRect(const Rect* deviceBounds) {
  auto& clip = mcStack->getClip();
  auto rect = Rect::MakeEmpty();
  if (clip.asRect(&rect)) {
    if (deviceBounds != nullptr && !rect.intersect(*deviceBounds)) {
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

std::shared_ptr<TextureProxy> Canvas::getClipTexture() {
  auto& clip = mcStack->getClip();
  auto domainID = PathRef::GetUniqueKey(clip).domainID();
  if (domainID == clipID) {
    return clipTexture;
  }
  auto bounds = clip.getBounds();
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto rasterizeMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  auto renderFlags = surfaceOptions()->renderFlags();
  if (ShouldTriangulatePath(clip, rasterizeMatrix)) {
    auto drawOp =
        TriangulatingPathOp::Make(Color::White(), clip, rasterizeMatrix, nullptr, renderFlags);
    auto renderTarget = RenderTargetProxy::Make(getContext(), width, height, PixelFormat::ALPHA_8);
    if (renderTarget == nullptr) {
      renderTarget = RenderTargetProxy::Make(getContext(), width, height, PixelFormat::RGBA_8888);
      if (renderTarget == nullptr) {
        return nullptr;
      }
    }
    auto drawingManager = getContext()->drawingManager();
    auto renderTask = drawingManager->addOpsTask(renderTarget);
    renderTask->addOp(std::move(drawOp));
    clipTexture = renderTarget->getTextureProxy();
  } else {
    auto uniqueKey = PathRef::GetUniqueKey(clip);
    auto rasterizer = Rasterizer::MakeFrom(clip, ISize::Make(width, height), rasterizeMatrix);
    auto proxyProvider = getContext()->proxyProvider();
    clipTexture = proxyProvider->createTextureProxy({}, rasterizer, false, renderFlags);
  }
  clipID = domainID;
  return clipTexture;
}

std::unique_ptr<FragmentProcessor> Canvas::getClipMask(const Rect& deviceBounds,
                                                       const Matrix& viewMatrix,
                                                       Rect* scissorRect) {
  auto& clip = mcStack->getClip();
  if (!clip.isEmpty() && clip.contains(deviceBounds)) {
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
  auto clipBounds = clip.getBounds();
  *scissorRect = clipBounds;
  FlipYIfNeeded(scissorRect, surface);
  scissorRect->roundOut();
  auto texture = getClipTexture();
  if (texture == nullptr) {
    return nullptr;
  }
  auto localMatrix = viewMatrix;
  localMatrix.postTranslate(-clipBounds.left, -clipBounds.top);
  auto maskEffect = TextureEffect::Make(texture, {}, &localMatrix);
  if (!texture->isAlphaOnly()) {
    maskEffect = FragmentProcessor::MulInputByChildAlpha(std::move(maskEffect));
  }
  return maskEffect;
}

static bool BlendModeIsOpaque(BlendMode mode, SrcColorOpacity opacityType) {
  BlendInfo blendInfo = {};
  if (!BlendModeAsCoeff(mode, &blendInfo)) {
    return false;
  }
  switch (blendInfo.srcBlend) {
    case BlendModeCoeff::DA:
    case BlendModeCoeff::DC:
    case BlendModeCoeff::IDA:
    case BlendModeCoeff::IDC:
      return false;
    default:
      break;
  }
  switch (blendInfo.dstBlend) {
    case BlendModeCoeff::Zero:
      return true;
    case BlendModeCoeff::ISA:
      return opacityType == SrcColorOpacity::Opaque;
    case BlendModeCoeff::SA:
      return opacityType == SrcColorOpacity::TransparentBlack ||
             opacityType == SrcColorOpacity::TransparentAlpha;
    case BlendModeCoeff::SC:
      return opacityType == SrcColorOpacity::TransparentBlack;
    default:
      return false;
  }
}

bool Canvas::wouldOverwriteEntireSurface(DrawOp* op, const DrawArgs& args,
                                         const FillStyle& style) const {
  if (op == nullptr || op->classID() != FillRectOp::ClassID()) {
    return false;
  }
  // Since wouldOverwriteEntireSurface() may not be complete free to call, we only do so if there is
  // a cached image snapshot in the surface.
  if (surface == nullptr || surface->cachedImage == nullptr) {
    return false;
  }
  auto clipRect = Rect::MakeEmpty();
  if (!mcStack->getClip().asRect(&clipRect) || !args.viewMatrix.rectStaysRect()) {
    return false;
  }
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  if (clipRect != surfaceRect) {
    return false;
  }
  auto deviceRect = args.viewMatrix.mapRect(args.drawRect);
  if (!deviceRect.contains(surfaceRect)) {
    return false;
  }
  if (style.maskFilter) {
    return false;
  }
  if (style.colorFilter && style.colorFilter->isAlphaUnchanged()) {
    return false;
  }
  auto opacityType = SrcColorOpacity::Unknown;
  auto alpha = style.color.alpha;
  if (alpha == 1.0f && (!style.shader || style.shader->isOpaque())) {
    opacityType = SrcColorOpacity::Opaque;
  } else if (alpha == 0) {
    if (style.shader) {
      opacityType = SrcColorOpacity::TransparentAlpha;
    } else {
      opacityType = SrcColorOpacity::TransparentBlack;
    }
  }
  return BlendModeIsOpaque(style.blendMode, opacityType);
}

void Canvas::addDrawOp(std::unique_ptr<DrawOp> op, const DrawArgs& args, const FillStyle& style) {
  if (op == nullptr || args.empty()) {
    return;
  }
  auto aaType = AAType::None;
  if (surface->renderTargetProxy->sampleCount() > 1) {
    aaType = AAType::MSAA;
  } else if (style.antiAlias) {
    auto isFillRect = op->classID() == FillRectOp::ClassID();
    if (!isFillRect || !args.viewMatrix.rectStaysRect() || !IsPixelAligned(op->bounds())) {
      aaType = AAType::Coverage;
    }
  }
  op->setAA(aaType);
  op->setBlendMode(style.blendMode);
  auto shaderFP = FragmentProcessor::Make(style.shader, args);
  if (shaderFP != nullptr) {
    op->addColorFP(std::move(shaderFP));
  }
  if (style.colorFilter) {
    if (auto processor = style.colorFilter->asFragmentProcessor()) {
      op->addColorFP(std::move(processor));
    }
  }
  if (style.maskFilter) {
    if (auto processor = style.maskFilter->asFragmentProcessor(args, nullptr)) {
      op->addCoverageFP(std::move(processor));
    }
  }
  Rect scissorRect = Rect::MakeEmpty();
  auto clipMask = getClipMask(op->bounds(), args.viewMatrix, &scissorRect);
  if (clipMask) {
    op->addCoverageFP(std::move(clipMask));
  }
  op->setScissorRect(scissorRect);
  auto discardContent = wouldOverwriteEntireSurface(op.get(), args, style);
  surface->addOp(std::move(op), discardContent);
}
}  // namespace tgfx
