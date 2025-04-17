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

#include <_types/_uint32_t.h>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include "core/TypefaceMetrics.h"
#include "pdf/PDFExportContext.h"
#include "pdf/PDFFont.h"
#include "pdf/PDFGraphicState.h"
#include "pdf/PDFTag.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Document.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

class PDFOffsetMap {
 public:
  void markStartOfDocument(const std::shared_ptr<WriteStream>& stream);
  void markStartOfObject(int referenceNumber, const std::shared_ptr<WriteStream>& stream);
  size_t objectCount() const;
  int emitCrossReferenceTable(const std::shared_ptr<WriteStream>& stream) const;

 private:
  std::vector<int> fOffsets;
  size_t fBaseOffset = SIZE_MAX;
};

struct SkPDFNamedDestination {
  std::shared_ptr<Data> fName;
  Point fPoint;
  PDFIndirectReference fPage;
};

struct PDFLink {
  enum class Type {
    kNone,
    kUrl,
    kNamedDestination,
  };

  PDFLink(Type type, std::shared_ptr<Data> data, const Rect& rect, int nodeId)
      : fType(type), fData(std::move(data)), fRect(rect), fNodeId(nodeId) {
  }

  const Type fType;
  const std::shared_ptr<Data> fData;
  const Rect fRect;
  const int fNodeId;
};

class PDFDocument : public Document {
 public:
  PDFDocument(std::shared_ptr<WriteStream> stream, Context* context, PDFMetadata Metadata);
  ~PDFDocument() override;
  Canvas* onBeginPage(float width, float height) override;
  void onEndPage() override;
  void onClose() override;
  void onAbort() override;

  static std::unique_ptr<Canvas> MakeCanvas(PDFExportContext* drawContext);

  Context* context() const {
    return _context;
  }

  /**
       Serialize the object, as well as any other objects it
       indirectly refers to.  If any any other objects have been added
       to the SkPDFObjNumMap without serializing them, they will be
       serialized as well.

       It might go without saying that objects should not be changed
       after calling serialize, since those changes will be too late.
     */

  PDFIndirectReference emit(const PDFObject& object, PDFIndirectReference ref);
  PDFIndirectReference emit(const PDFObject& object) {
    return this->emit(object, this->reserveRef());
  }

  template <typename T>
  void emitStream(const PDFDictionary& dict, T writeStream, PDFIndirectReference ref) {
    std::lock_guard<std::mutex> autoLock(fMutex);
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

  //   PDFIndirectReference currentPage() const {
  //     return SkASSERT(this->hasCurrentPage() && !fPageRefs.empty()), fPageRefs.back();
  //   }

  //   // Used to allow marked content to refer to its corresponding structure
  //   // tree node, via a page entry in the parent tree. Returns -1 if no
  //   // mark ID.
  //   SkPDFTagTree::Mark createMarkIdForNodeId(int nodeId, SkPoint);
  //   // Used to allow annotations to refer to their corresponding structure
  //   // tree node, via the struct parent tree. Returns -1 if no struct parent
  //   // key.
  //   int createStructParentKeyForNodeId(int nodeId);

  //   void addNodeTitle(int nodeId, SkSpan<const char>);

  //   std::unique_ptr<SkPDFArray> getAnnotations();

  PDFIndirectReference reserveRef() {
    return PDFIndirectReference{fNextObjectNumber++};
  }

  // Returns a tag to prepend to a PostScript name of a subset font. Includes the '+'.
  std::string nextFontSubsetTag();

  // SkExecutor* executor() const {
  //   return fExecutor;
  // }

  void incrementJobCount();
  void signalJobComplete();
  size_t currentPageIndex() {
    return fPages.size();
  }
  size_t pageCount() {
    return fPageRefs.size();
  }

  const Matrix& currentPageTransform() const;

  //   // Canonicalized objects
  //   std::unordered_map<SkPDFImageShaderKey, PDFIndirectReference, SkPDFImageShaderKey::Hash>
  //       fImageShaderMap;
  //   skia_private::THashMap<SkPDFGradientShader::Key, SkPDFIndirectReference,
  //                          SkPDFGradientShader::KeyHash>
  //       fGradientPatternMap;
  //   skia_private::THashMap<SkBitmapKey, SkPDFIndirectReference> fPDFBitmapMap;
  //   skia_private::THashMap<SkPDFIccProfileKey, SkPDFIndirectReference, SkPDFIccProfileKey::Hash>
  //       fICCProfileMap;
  std::unordered_map<uint32_t, std::unique_ptr<TypefaceMetrics>> fTypefaceMetrics;
  std::unordered_map<uint32_t, std::vector<std::string>> fType1GlyphNames;
  std::unordered_map<uint32_t, std::vector<Unichar>> fToUnicodeMap;
  std::unordered_map<uint32_t, PDFIndirectReference> fFontDescriptors;
  std::unordered_map<uint32_t, PDFIndirectReference> fType3FontDescriptors;
  std::unordered_map<uint32_t, std::shared_ptr<PDFStrike>> fStrikes;
  //   skia_private::THashMap<SkPDFStrokeGraphicState, SkPDFIndirectReference,
  //                          SkPDFStrokeGraphicState::Hash>
  //       fStrokeGSMap;
  std::unordered_map<PDFFillGraphicState, PDFIndirectReference> fFillGSMap;
  //   SkPDFIndirectReference fInvertFunction;
  PDFIndirectReference fNoSmaskGraphicState;
  //   std::vector<std::unique_ptr<SkPDFLink>> fCurrentPageLinks;
  std::vector<SkPDFNamedDestination> fNamedDestinations;

 private:
  Context* _context = nullptr;
  PDFOffsetMap fOffsetMap;
  Canvas* canvas = nullptr;
  PDFExportContext* drawContext = nullptr;

  std::vector<std::unique_ptr<PDFDictionary>> fPages;
  std::vector<PDFIndirectReference> fPageRefs;

  //   sk_sp<SkPDFDevice> fPageDevice;
  std::atomic<int> fNextObjectNumber = {1};
  // std::atomic<int> fJobCount = {0};
  uint32_t fNextFontSubsetTag = {0};
  UUID fUUID;
  PDFIndirectReference fInfoDict;
  PDFIndirectReference fXMP;
  PDFMetadata _metadata;
  float fRasterScale = 1;
  float fInverseRasterScale = 1;
  //   SkExecutor* fExecutor = nullptr;

  // For tagged PDFs.
  SkPDFTagTree fTagTree;

  std::mutex fMutex;
  // SkSemaphore fSemaphore;

  void waitForJobs();

  std::shared_ptr<WriteStream> beginObject(PDFIndirectReference ref);
  void endObject();
};

}  // namespace tgfx