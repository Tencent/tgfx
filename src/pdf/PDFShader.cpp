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

#include "PDFShader.h"
#include "core/shaders/ImageShader.h"
#include "core/shaders/MatrixShader.h"
#include "core/utils/CopyPixels.h"
#include "core/utils/Log.h"
#include "core/utils/Types.h"
#include "pdf/PDFDocumentImpl.h"
#include "pdf/PDFExportContext.h"
#include "pdf/PDFGradientShader.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/SurfaceReadback.h"
#include "tgfx/core/TileMode.h"

namespace tgfx {

namespace {
void Draw(Canvas* canvas, std::shared_ptr<Image> image, Color paintColor) {
  Paint paint;
  paint.setColor(paintColor);
  canvas->drawImage(std::move(image), SamplingOptions(), &paint);
}

Bitmap ImageExportToBitmap(Context* context, const std::shared_ptr<Image>& image,
                           std::shared_ptr<ColorSpace> colorSpace) {
  auto surface = Surface::Make(context, image->width(), image->height(), false, 1, false, 0,
                               std::move(colorSpace));
  if (surface == nullptr) {
    LOGE("PDFShader ImageExportToBitmap() Failed to create the surface! (%dx%d)", image->width(),
         image->height());
    return {};
  }
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);

  // Deliberately not Surface::readPixels(): it cannot tell a pending readback apart from a real
  // failure and reports both as an error. Here a pending one degrades quietly and lets the caller
  // report the resulting content gap, while a ready buffer yielding nothing stays a failure.
  auto readback = surface->asyncReadPixels(Rect::MakeWH(surface->width(), surface->height()));
  if (readback == nullptr) {
    LOGE("PDFShader ImageExportToBitmap() Failed to start the readback! (%dx%d)", surface->width(),
         surface->height());
    return {};
  }
  auto* srcPixels = readback->lockPixels(context);
  if (srcPixels == nullptr) {
    if (readback->isReady(context)) {
      LOGE("PDFShader ImageExportToBitmap() Failed to read the pixels from the surface!");
    }
    return {};
  }

  Bitmap bitmap(surface->width(), surface->height(), false, true, surface->colorSpace());
  auto* dstPixels = bitmap.lockPixels();
  if (dstPixels == nullptr) {
    // The caller only checks isEmpty(), so an allocated but unwritten Bitmap must not escape: its
    // uninitialized memory would end up in the clamp edge colors.
    LOGE("PDFShader ImageExportToBitmap() Failed to lock the bitmap pixels!");
    readback->unlockPixels(context);
    return {};
  }
  // The readback row stride carries the backend's buffer copy alignment (256 bytes on WebGPU) and
  // can exceed the Bitmap stride, so both layouts have to be walked.
  CopyPixels(readback->info(), srcPixels, bitmap.info(), dstPixels,
             surface->origin() == ImageOrigin::BottomLeft);
  bitmap.unlockPixels();
  readback->unlockPixels(context);
  return bitmap;
}

void DrawMatrix(Canvas* canvas, std::shared_ptr<Image> image, const Matrix& matrix,
                Color paintColor) {
  AutoCanvasRestore acr(canvas);
  canvas->concat(matrix);
  Draw(canvas, std::move(image), paintColor);
}

void DrawBitmapMatrix(Canvas* canvas, const Bitmap& bm, const Matrix& matrix, Color paintColor) {
  AutoCanvasRestore acr(canvas);
  canvas->concat(matrix);
  Paint paint;
  paint.setColor(paintColor);
  auto image = Image::MakeFrom(bm);
  canvas->drawImage(image, SamplingOptions(), &paint);
}

void FillColorFromBitmap(Canvas* canvas, float left, float top, float right, float bottom,
                         const Bitmap& bitmap, int x, int y, float alpha) {
  Rect rect{left, top, right, bottom};
  if (!rect.isEmpty()) {
    Color color = bitmap.getColor(x, y);
    color.alpha = alpha * color.alpha;
    Paint paint;
    paint.setColor(color);
    canvas->drawRect(rect, paint);
  }
}

Color AdjustColor(const Shader* shader, Color paintColor) {
  if (Types::Get(shader) == Types::ShaderType::Image) {
    const auto imageShader = static_cast<const ImageShader*>(shader);
    const auto img = imageShader->image.get();
    if (img && img->isAlphaOnly()) {
      return paintColor;
    }
  }
  return Color{0, 0, 0, paintColor.alpha};  // only preserve the alpha.
}

bool IsTiled(TileMode mode) {
  return mode == TileMode::Mirror || mode == TileMode::Repeat;
}

Matrix ScaleTranslate(float sx, float sy, float tx, float ty) {
  Matrix matrix;
  matrix.setAll(sx, 0.f, tx, 0.f, sy, ty);
  return matrix;
}

Bitmap ExtractSubset(Bitmap src, Rect subset) {
  Bitmap destination(static_cast<int>(subset.width()), static_cast<int>(subset.height()), false,
                     true, src.colorSpace());
  destination.clear();
  const auto srcPixels = src.lockPixels();
  destination.writePixels(src.info(), srcPixels, static_cast<int>(subset.left),
                          static_cast<int>(subset.top));
  src.unlockPixels();
  return destination;
}
}  // namespace

PDFIndirectReference PDFShader::Make(PDFDocumentImpl* doc, const std::shared_ptr<Shader>& shader,
                                     const Matrix& canvasTransform, const Rect& surfaceBBox,
                                     Color paintColor) {
  DEBUG_ASSERT(shader);
  DEBUG_ASSERT(doc);
  // Unwrap MatrixShader layers to find the underlying shader and accumulate the matrix.
  auto unwrappedShader = shader;
  auto combinedTransform = canvasTransform;
  while (Types::Get(unwrappedShader.get()) == Types::ShaderType::Matrix) {
    const auto matrixShader = static_cast<const MatrixShader*>(unwrappedShader.get());
    combinedTransform.preConcat(matrixShader->matrix);
    unwrappedShader = matrixShader->source;
  }
  if (Types::Get(unwrappedShader.get()) == Types::ShaderType::Gradient) {
    const auto gradientShader = static_cast<const GradientShader*>(unwrappedShader.get());
    DEBUG_ASSERT(gradientShader->asGradient(nullptr) == GradientType::Linear ||
                 gradientShader->asGradient(nullptr) == GradientType::Radial ||
                 gradientShader->asGradient(nullptr) == GradientType::Diamond ||
                 gradientShader->asGradient(nullptr) == GradientType::Conic);
    return PDFGradientShader::Make(doc, gradientShader, combinedTransform, surfaceBBox);
  }
  if (surfaceBBox.isEmpty()) {
    return PDFIndirectReference();
  }

  paintColor = AdjustColor(unwrappedShader.get(), paintColor);

  if (Types::Get(unwrappedShader.get()) == Types::ShaderType::Image) {
    const auto imageShader = static_cast<const ImageShader*>(unwrappedShader.get());
    auto shaderImage = imageShader->image;
    // TODO (YGaurora): Cache image shaders and remove duplicates
    PDFIndirectReference pdfShader =
        MakeImageShader(doc, combinedTransform, imageShader->tileModeX, imageShader->tileModeY,
                        surfaceBBox, shaderImage, paintColor);
    return pdfShader;
  }
  // Don't bother to de-dup fallback shader.
  return MakeFallbackShader(doc, unwrappedShader, combinedTransform, surfaceBBox, paintColor);
}

PDFIndirectReference PDFShader::MakeImageShader(PDFDocumentImpl* doc, Matrix finalMatrix,
                                                TileMode tileModesX, TileMode tileModesY, Rect bBox,
                                                const std::shared_ptr<Image>& image,
                                                Color paintColor) {
  // The image shader pattern cell will be drawn into a separate device
  // in pattern cell space (no scaling on the bitmap, though there may be
  // translations so that all content is in the device, coordinates > 0).

  // Map clip bounds to shader space to ensure the device is large enough
  // to handle fake clamping.

  Rect deviceBounds = bBox;
  if (!PDFUtils::InverseTransformBBox(finalMatrix, &deviceBounds)) {
    return PDFIndirectReference();
  }

  Rect bitmapBounds = Rect::MakeWH(image->width(), image->height());

  // For tiling modes, the bounds should be extended to include the bitmap,
  // otherwise the bitmap gets clipped out and the shader is empty and awful.
  // For clamp modes, we're only interested in the clip region, whether
  // or not the main bitmap is in it.
  if (IsTiled(tileModesX) || IsTiled(tileModesY)) {
    deviceBounds.join(bitmapBounds);
  }

  ISize patternDeviceSize = {static_cast<int>(std::ceil(deviceBounds.width())),
                             static_cast<int>(std::ceil(deviceBounds.height()))};
  auto patternContext = PDFExportContext(patternDeviceSize, doc);
  auto canvas = PDFDocumentImpl::MakeCanvas(&patternContext);

  auto patternBBox = Rect::MakeWH(image->width(), image->height());
  float width = patternBBox.width();
  float height = patternBBox.height();

  // Translate the canvas so that the bitmap origin is at (0, 0).
  canvas->translate(-deviceBounds.left, -deviceBounds.top);
  patternBBox.offset(-deviceBounds.left, -deviceBounds.top);
  // Undo the translation in the final matrix
  finalMatrix.preTranslate(deviceBounds.left, deviceBounds.top);

  // If the bitmap is out of bounds (i.e. clamp mode where we only see the
  // stretched sides), canvas will clip this out and the extraneous data
  // won't be saved to the PDF.
  Draw(canvas.get(), image, paintColor);

  // Tiling is implied.  First we handle mirroring.
  if (tileModesX == TileMode::Mirror) {
    DrawMatrix(canvas.get(), image, ScaleTranslate(-1, 1, 2 * width, 0), paintColor);
    patternBBox.right += width;
  }
  if (tileModesY == TileMode::Mirror) {
    DrawMatrix(canvas.get(), image, ScaleTranslate(1, -1, 0, 2 * height), paintColor);
    patternBBox.bottom += height;
  }
  if (tileModesX == TileMode::Mirror && tileModesY == TileMode::Mirror) {
    DrawMatrix(canvas.get(), image, ScaleTranslate(-1, -1, 2 * width, 2 * height), paintColor);
  }

  // Then handle Clamping, which requires expanding the pattern canvas to
  // cover the entire surfaceBBox.

  Bitmap bitmap;
  if (tileModesX == TileMode::Clamp || tileModesY == TileMode::Clamp) {
    // For now, the easiest way to access the colors in the corners and sides is
    // to just make a bitmap from the image.
    bitmap = ImageExportToBitmap(doc->context(), image, doc->dstColorSpace());
  }
  // Reading the pixels back can fail, for instance on a backend without synchronous readback. The
  // clamp handling below indexes bitmap.width() - 1, which would be -1 on an empty bitmap, so drop
  // the stretched edges and emit the pattern body alone.
  if (bitmap.isEmpty() && (tileModesX == TileMode::Clamp || tileModesY == TileMode::Clamp)) {
    LOGE("PDFShader::MakeImageShader() Clamp edges are dropped, the pixels are unavailable!");
  }
  const auto clampEdgesX = tileModesX == TileMode::Clamp && !bitmap.isEmpty();
  const auto clampEdgesY = tileModesY == TileMode::Clamp && !bitmap.isEmpty();

  // If both x and y are in clamp mode, we start by filling in the corners.
  // (Which are just a rectangles of the corner colors.)
  if (clampEdgesX && clampEdgesY) {
    FillColorFromBitmap(canvas.get(), deviceBounds.left, deviceBounds.top, 0, 0, bitmap, 0, 0,
                        paintColor.alpha);

    FillColorFromBitmap(canvas.get(), width, deviceBounds.top, deviceBounds.right, 0, bitmap,
                        bitmap.width() - 1, 0, paintColor.alpha);

    FillColorFromBitmap(canvas.get(), width, height, deviceBounds.right, deviceBounds.bottom,
                        bitmap, bitmap.width() - 1, bitmap.height() - 1, paintColor.alpha);

    FillColorFromBitmap(canvas.get(), deviceBounds.left, height, 0, deviceBounds.bottom, bitmap, 0,
                        bitmap.height() - 1, paintColor.alpha);
  }

  // Then expand the left, right, top, then bottom.
  if (clampEdgesX) {
    auto subset = Rect::MakeXYWH(0, 0, 1, bitmap.height());
    if (deviceBounds.left < 0) {
      Bitmap left = ExtractSubset(bitmap, subset);
      auto leftMatrix = ScaleTranslate(-deviceBounds.left, 1, deviceBounds.left, 0);
      DrawBitmapMatrix(canvas.get(), left, leftMatrix, paintColor);

      if (tileModesY == TileMode::Mirror) {
        leftMatrix.postScale(1.f, -1.f);
        leftMatrix.postTranslate(0, 2 * height);
        DrawBitmapMatrix(canvas.get(), left, leftMatrix, paintColor);
      }
      patternBBox.left = 0;
    }

    if (deviceBounds.right > width) {
      subset.offset(static_cast<float>(bitmap.width()) - 1.f, 0.f);
      Bitmap right = ExtractSubset(bitmap, subset);
      auto rightMatrix = ScaleTranslate(deviceBounds.right - width, 1, width, 0);
      DrawBitmapMatrix(canvas.get(), right, rightMatrix, paintColor);

      if (tileModesY == TileMode::Mirror) {
        rightMatrix.postScale(1.f, -1.f);
        rightMatrix.postTranslate(0, 2 * height);
        DrawBitmapMatrix(canvas.get(), right, rightMatrix, paintColor);
      }
      patternBBox.right = deviceBounds.width();
    }
  }
  if (tileModesX == TileMode::Decal) {
    if (deviceBounds.left < 0) {
      patternBBox.left = 0;
    }
    if (deviceBounds.right > width) {
      patternBBox.right = deviceBounds.width();
    }
  }

  if (clampEdgesY) {
    auto subset = Rect::MakeXYWH(0, 0, bitmap.width(), 1);
    if (deviceBounds.top < 0) {
      Bitmap top = ExtractSubset(bitmap, subset);
      auto topMatrix = ScaleTranslate(1, -deviceBounds.top, 0, deviceBounds.top);
      DrawBitmapMatrix(canvas.get(), top, topMatrix, paintColor);

      if (tileModesX == TileMode::Mirror) {
        topMatrix.postScale(-1, 1);
        topMatrix.postTranslate(2 * width, 0);
        DrawBitmapMatrix(canvas.get(), top, topMatrix, paintColor);
      }
      patternBBox.top = 0;
    }

    if (deviceBounds.bottom > height) {
      subset.offset(0.f, static_cast<float>(bitmap.height()) - 1.f);
      Bitmap bottom = ExtractSubset(bitmap, subset);

      auto bottomMatrix = ScaleTranslate(1, deviceBounds.bottom - height, 0, height);
      DrawBitmapMatrix(canvas.get(), bottom, bottomMatrix, paintColor);

      if (tileModesX == TileMode::Mirror) {
        bottomMatrix.postScale(-1, 1);
        bottomMatrix.postTranslate(2 * width, 0);
        DrawBitmapMatrix(canvas.get(), bottom, bottomMatrix, paintColor);
      }
      patternBBox.bottom = deviceBounds.height();
    }
  }
  if (tileModesY == TileMode::Decal) {
    if (deviceBounds.top < 0) {
      patternBBox.top = 0;
    }
    if (deviceBounds.bottom > height) {
      patternBBox.bottom = deviceBounds.height();
    }
  }

  auto shaderData = patternContext.getContent();
  auto resourceDict = patternContext.makeResourceDictionary();
  auto dict = PDFDictionary::Make();
  PDFUtils::PopulateTilingPatternDict(dict.get(), patternBBox, std::move(resourceDict),
                                      finalMatrix);
  auto stream = Stream::MakeFromData(shaderData);
  return PDFStreamOut(std::move(dict), std::move(stream), doc);
}

// Generic fallback for unsupported shaders:
//  * allocate a surfaceBBox-sized bitmap
//  * shade the whole area
//  * use the result as a bitmap shader
PDFIndirectReference PDFShader::MakeFallbackShader(PDFDocumentImpl* doc,
                                                   const std::shared_ptr<Shader>& shader,
                                                   const Matrix& canvasTransform,
                                                   const Rect& surfaceBBox, Color paintColor) {
  // surfaceBBox is in device space. While that's exactly what we want for sizing our bitmap,
  // need to map it into shader space for adjustments (to match MakeImageShader's behavior).
  auto shaderRect = surfaceBBox;
  if (!PDFUtils::InverseTransformBBox(canvasTransform, &shaderRect)) {
    return PDFIndirectReference();
  }
  // Clamp the bitmap size to about 1M pixels
  constexpr float MaxBitmapArea = 1024 * 1024;
  float bitmapArea = surfaceBBox.width() * surfaceBBox.height();
  float rasterScale = 1.0f;
  if (bitmapArea > MaxBitmapArea) {
    rasterScale *= std::sqrt(MaxBitmapArea / bitmapArea);
  }

  auto ceilSize = [MaxBitmapArea](float x) -> int {
    return static_cast<int>(std::clamp(std::ceil(x), 1.0f, MaxBitmapArea));
  };

  ISize size = {ceilSize(rasterScale * surfaceBBox.width()),
                ceilSize(rasterScale * surfaceBBox.height())};
  Size scale = {static_cast<float>(size.width) / shaderRect.width(),
                static_cast<float>(size.height) / shaderRect.height()};

  auto surface = Surface::Make(doc->context(), size.width, size.height, false, 1, false, 0,
                               doc->dstColorSpace());
  DEBUG_ASSERT(surface);
  Canvas* canvas = surface->getCanvas();
  canvas->clear(Color::Transparent());

  Paint paint;
  paint.setColor(paintColor);
  paint.setShader(std::move(shader));

  canvas->scale(scale.width, scale.height);
  canvas->translate(-shaderRect.x(), -shaderRect.y());
  canvas->drawPaint(paint);

  auto shaderTransform = Matrix::MakeTrans(shaderRect.x(), shaderRect.y());
  shaderTransform.preScale(1 / scale.width, 1 / scale.height);

  auto image = surface->makeImageSnapshot();
  DEBUG_ASSERT(image);
  return MakeImageShader(doc, canvasTransform * shaderTransform, TileMode::Clamp, TileMode::Clamp,
                         surfaceBBox, image, paintColor);
}

}  // namespace tgfx
