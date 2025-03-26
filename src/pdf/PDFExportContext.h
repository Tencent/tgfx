/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <memory>
#include <unordered_set>
#include "core/DrawContext.h"
#include "core/FillStyle.h"
#include "core/MCState.h"
#include "core/Records.h"
#include "core/filters/BlurImageFilter.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "pdf/PDFGraphicStackState.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

class PDFExportContext : public DrawContext {
 public:
  explicit PDFExportContext(ISize pageSize, PDFDocument* document,
                            const Matrix& transform = Matrix::I());

  ~PDFExportContext() override;

  void drawStyle(const MCState& state, const FillStyle& style) override;

  void drawRect(const Rect& rect, const MCState& state, const FillStyle& style) override;

  void drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                 const FillStyle& style) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const FillStyle& style) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state,
                     const FillStyle& style) override;

  void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const Stroke* stroke,
                        const MCState& state, const FillStyle& style) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const FillStyle& style) override;

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

  void onDrawPath(const MCState& state, const Matrix& ctm, const Path& path,
                  const FillStyle& srcStyle, bool pathIsMutable);

  void onDrawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                       const SamplingOptions& sampling, const MCState& state,
                       const FillStyle& srcStyle);

  std::shared_ptr<MemoryWriteStream> setUpContentEntry(const MCState* state, const Matrix& matrix,
                                                       const FillStyle& style, float scale,
                                                       PDFIndirectReference* destination);

  void finishContentEntry(const MCState* state, BlendMode blendMode,
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

  void drawFormXObject(PDFIndirectReference xObject, std::shared_ptr<MemoryWriteStream> stream,
                       Path* shape);

  void drawDropShadowBeforeLayer(const std::shared_ptr<Picture>& picture,
                                 const DropShadowImageFilter* dropShadowFilter,
                                 const MCState& state, const FillStyle& style);

  static void DrawInnerShadowAfterLayer(const std::shared_ptr<Picture>& picture,
                                        const std::shared_ptr<ImageFilter>& imageFilter,
                                        const MCState& state, const FillStyle& style,
                                        Context* context, PDFExportContext* pdfExportContext);

  void drawInnerShadowAfterLayer(const Record* record,
                                 const InnerShadowImageFilter* innerShadowFilter,
                                 const MCState& state);

  void drawBlurLayer(const std::shared_ptr<Picture>& picture,
                     const std::shared_ptr<ImageFilter>& imageFilter, const MCState& state,
                     const FillStyle& style);

  void drawPathWithFilter(const MCState& clipStack, const Matrix& matrix, const Path& originPath,
                          const FillStyle& originPaint);

  ISize _pageSize = {};
  // uint32_t nodeId = 0;
  PDFDocument* document = nullptr;
  Matrix _initialTransform = {};

  bool needsExtraSave = false;
  std::shared_ptr<MemoryWriteStream> content = nullptr;
  std::shared_ptr<MemoryWriteStream> contentBuffer = nullptr;
  PDFGraphicStackState fActiveStackState;

  std::unordered_set<PDFIndirectReference> fGraphicStateResources;
  std::unordered_set<PDFIndirectReference> fXObjectResources;
  std::unordered_set<PDFIndirectReference> fShaderResources;
  std::unordered_set<PDFIndirectReference> fFontResources;

  friend class ScopedContentEntry;
};

}  // namespace tgfx