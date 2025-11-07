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

#include "core/AdvancedTypefaceInfo.h"
#include "pdf/PDFExportContext.h"
#include "pdf/PDFFont.h"
#include "pdf/PDFGraphicState.h"
#include "pdf/PDFTag.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/pdf/PDFDocument.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

class PDFOffsetMap {
 public:
  void markStartOfDocument(const std::shared_ptr<WriteStream>& stream);

  void markStartOfObject(int referenceNumber, const std::shared_ptr<WriteStream>& stream);

  size_t objectCount() const;

  int emitCrossReferenceTable(const std::shared_ptr<WriteStream>& stream) const;

 private:
  std::vector<int> offsets;
  size_t baseOffset = SIZE_MAX;
};

struct PDFNamedDestination {
  std::shared_ptr<Data> name;
  Point point;
  PDFIndirectReference page;
};

struct PDFLink {
  enum class Type {
    None,
    Url,
    NamedDestination,
  };

  PDFLink(Type type, std::shared_ptr<Data> data, const Rect& rect, int nodeId)
      : type(type), data(std::move(data)), rect(rect), nodeId(nodeId) {
  }

  const Type type;
  const std::shared_ptr<Data> data;
  const Rect rect;
  const int nodeId;
};

class PDFDocumentImpl : public PDFDocument {
 public:
  PDFDocumentImpl(std::shared_ptr<WriteStream> stream, Context* context, PDFMetadata Metadata);

  ~PDFDocumentImpl() override;

  Canvas* beginPage(float pageWidth, float pageHeight, const Rect* contentRect) override;

  void endPage() override;

  void close() override;

  void abort() override;

  Canvas* onBeginPage(float width, float height);

  void onEndPage();

  void onClose();

  void onAbort();

  static std::unique_ptr<Canvas> MakeCanvas(PDFExportContext* drawContext);

  Context* context() const {
    return _context;
  }

  PDFIndirectReference emit(const PDFObject& object, PDFIndirectReference ref);

  PDFIndirectReference emit(const PDFObject& object) {
    return this->emit(object, this->reserveRef());
  }

  template <typename T>
  void emitStream(const PDFDictionary& dict, T writeStream, PDFIndirectReference ref) {
    auto stream = this->beginObject(ref);
    dict.emitObject(stream);
    stream->writeText(" stream\n");
    writeStream(stream);
    stream->writeText("\nendstream");
    this->endObject();
  }

  const PDFMetadata& metadata() const {
    return _metadata;
  }

  PDFIndirectReference getPage(size_t pageIndex) const;

  bool hasCurrentPage() const {
    return drawContext != nullptr;
  }

  PDFIndirectReference reserveRef() {
    return PDFIndirectReference{nextObjectNumber++};
  }

  // Returns a tag to prepend to a PostScript name of a subset font. Includes the '+'.
  std::string nextFontSubsetTag();

  size_t currentPageIndex() {
    return pages.size();
  }

  size_t pageCount() {
    return pageRefs.size();
  }

  Canvas* canvas() const {
    return _canvas;
  }

  const Matrix& currentPageTransform() const;

  std::shared_ptr<ColorSpace> colorSpace() const {
    return _metadata.colorSpace;
  }

  PDFIndirectReference colorSpaceRef() const {
    return _colorSpaceRef;
  }

  std::unordered_map<uint32_t, std::unique_ptr<AdvancedTypefaceInfo>> fontAdvancedInfo;
  std::unordered_map<uint32_t, std::vector<std::string>> type1GlyphNames;
  std::unordered_map<uint32_t, std::vector<Unichar>> toUnicodeMap;
  std::unordered_map<uint32_t, PDFIndirectReference> fontDescriptors;
  std::unordered_map<uint32_t, PDFIndirectReference> type3FontDescriptors;
  std::unordered_map<uint32_t, std::shared_ptr<PDFStrike>> strikes;
  std::unordered_map<PDFFillGraphicState, PDFIndirectReference> fillGSMap;
  PDFIndirectReference noSmaskGraphicState;
  std::vector<PDFNamedDestination> namedDestinations;

 private:
  std::shared_ptr<WriteStream> beginObject(PDFIndirectReference ref);

  void endObject();

  PDFIndirectReference emitColorSpace();

  enum class State {
    BetweenPages,
    InPage,
    Closed,
  };

  State state = State::BetweenPages;
  std::shared_ptr<WriteStream> _stream = nullptr;

  Context* _context = nullptr;
  PDFOffsetMap offsetMap;
  Canvas* _canvas = nullptr;
  PDFExportContext* drawContext = nullptr;
  std::vector<std::unique_ptr<PDFDictionary>> pages;
  std::vector<PDFIndirectReference> pageRefs;
  std::atomic<int> nextObjectNumber = {1};
  uint32_t nextFontSubsetTag_ = {0};
  UUID documentUUID;
  PDFIndirectReference infoDictionary;
  PDFIndirectReference documentXMP;
  PDFMetadata _metadata;
  float rasterScale = 1;
  float inverseRasterScale = 1;
  PDFTagTree tagTree;
  PDFIndirectReference _colorSpaceRef;
};

}  // namespace tgfx