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

#pragma once

#include <unordered_set>
#include "core/DrawContext.h"
#include "core/PictureRecords.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "pdf/PDFGraphicStackState.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Brush.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/GlyphRun.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

class PDFExportContext : public DrawContext {
 public:
  explicit PDFExportContext(ISize pageSize, PDFDocumentImpl* document,
                            const Matrix& transform = Matrix::I());

  ~PDFExportContext() override;

  void drawFill(const Brush& brush) override;

  void drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip, const Brush& brush,
                const Stroke* stroke) override;

  void drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override;

  void drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                const Brush& brush) override;

  void drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override;

  void drawMesh(std::shared_ptr<Mesh>, const Matrix&, const ClipStack&, const Brush&) override {
    // PDF does not support mesh rendering.
  }

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const Matrix& matrix, const ClipStack& clip, const Brush& brush) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const Matrix& matrix, const ClipStack& clip,
                     const Brush& brush, SrcRectConstraint constraint) override;

  void drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix, const ClipStack& clip,
                    const Brush& brush, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                   const ClipStack& clip) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const Matrix& matrix, const ClipStack& clip, const Brush& brush) override;

  std::unique_ptr<PDFDictionary> makeResourceDictionary();

  std::shared_ptr<Data> getContent();

  ISize pageSize() const {
    return _pageSize;
  }

  const Matrix& initialTransform() {
    return _initialTransform;
  }

  std::unique_ptr<PDFDictionary> makeResourceDict();

 private:
  void reset();

  void onDrawPath(const Matrix& matrix, const ClipStack& clip, const Path& path,
                  const Brush& brush);

  void onDrawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                       const SamplingOptions& sampling, const Matrix& matrix, const ClipStack& clip,
                       const Brush& brush);

  void onDrawGlyphRun(const GlyphRun& glyphRun, const Matrix& matrix, const ClipStack& clip,
                      const Brush& brush, const Stroke* stroke);

  void exportGlyphRunAsText(const GlyphRun& glyphRun, const Matrix& matrix, const ClipStack& clip,
                            const Brush& brush);

  void exportGlyphRunAsPath(const GlyphRun& glyphRun, const Matrix& matrix, const ClipStack& clip,
                            const Brush& brush, const Stroke* stroke);

  void exportGlyphRunAsImage(const GlyphRun& glyphRun, const Matrix& matrix, const ClipStack& clip,
                             const Brush& brush);

  std::shared_ptr<MemoryWriteStream> setUpContentEntry(const Matrix& matrix, const ClipStack& clip,
                                                       const Matrix& contentExtraMatrix,
                                                       const Brush& brush, float scale,
                                                       PDFIndirectReference* destination);

  void finishContentEntry(const Matrix& matrix, const ClipStack& clip, BlendMode blendMode,
                          PDFIndirectReference destination, Path* path);

  bool isContentEmpty();

  std::shared_ptr<PDFExportContext> makeCongruentDevice() {
    return std::make_shared<PDFExportContext>(pageSize(), document);
  }

  PDFIndirectReference makeFormXObjectFromDevice(bool alpha = false);

  PDFIndirectReference makeFormXObjectFromDevice(Rect bounds, bool alpha = false);

  void drawFormXObjectWithMask(PDFIndirectReference xObject, PDFIndirectReference sMask,
                               BlendMode mode, bool invertClip);

  void clearMaskOnGraphicState(const std::shared_ptr<MemoryWriteStream>& stream);

  void setGraphicState(PDFIndirectReference graphicState,
                       const std::shared_ptr<MemoryWriteStream>& stream);

  void drawFormXObject(PDFIndirectReference xObject,
                       const std::shared_ptr<MemoryWriteStream>& stream, Path* shape);

  void drawDropShadowBeforeLayer(const std::shared_ptr<Picture>& picture,
                                 const DropShadowImageFilter* dropShadowFilter,
                                 const Matrix& matrix, const ClipStack& clip, const Brush& brush);

  void drawInnerShadowAfterLayer(const PictureRecord* record,
                                 const InnerShadowImageFilter* innerShadowFilter,
                                 const Matrix& matrix, const ClipStack& clip,
                                 const Matrix& currentMatrix);

  void drawBlurLayer(const std::shared_ptr<Picture>& picture,
                     const std::shared_ptr<ImageFilter>& imageFilter, const Matrix& matrix,
                     const ClipStack& clip, const Brush& brush);

  void drawPathWithFilter(const Matrix& matrix, const ClipStack& clip, const Path& originPath,
                          const Matrix& pathExtraMatrix, const Brush& originPaint);

  // Plan X: emit a bitmap-with-mask as an ordinary Image XObject + SMask form (instead of
  // routing through PDFShader's tiling-pattern path). `image`, `rect`, `matrix`, `clip` and
  // `brush` carry the same semantics as in onDrawImageRect; `transform` is the image-pixel ->
  // path-local mapping computed by the caller. The brush's maskFilter must be non-null on
  // entry; this function consumes it (the caller should not also draw the image again).
  void drawImageRectWithMaskFilter(const std::shared_ptr<Image>& image, const Rect& rect,
                                   const Matrix& matrix, const ClipStack& clip, const Brush& brush,
                                   const Matrix& transform);

  // Build an SMask form XObject from `maskFilter` for the rectangle `rect` (in path-local
  // space), assuming the rectangle is later drawn under `transform` (image-pixel -> path-local).
  // Returns an empty reference if the mask filter cannot be represented as an SMask form.
  // The produced form lives in this context's set-time user space, so it can be installed via
  // setGraphicState() and aligns with subsequent draws on the same content stream.
  PDFIndirectReference MakeMaskFormXObjectForRect(const std::shared_ptr<MaskFilter>& maskFilter,
                                                  const Rect& rect, const Matrix& transform);

  ISize _pageSize = {};
  PDFDocumentImpl* document = nullptr;
  Matrix _initialTransform = {};

  bool needsExtraSave = false;
  std::shared_ptr<MemoryWriteStream> content = nullptr;
  std::shared_ptr<MemoryWriteStream> contentBuffer = nullptr;
  PDFGraphicStackState activeStackState;

  std::unordered_set<PDFIndirectReference> graphicStateResources;
  std::unordered_set<PDFIndirectReference> xObjectResources;
  std::unordered_set<PDFIndirectReference> shaderResources;
  std::unordered_set<PDFIndirectReference> fontResources;

  friend class ScopedContentEntry;
  friend class PDFFont;
};

}  // namespace tgfx
