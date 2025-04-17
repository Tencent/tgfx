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

#include "PDFDocument.h"
#include <_types/_uint8_t.h>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include "core/utils/Log.h"
#include "pdf/PDFMetadataUtils.h"
#include "pdf/PDFTypes.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/UTF.h"
#include "tgfx/core/WriteStream.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

namespace {
size_t difference(size_t minuend, size_t subtrahend) {
  DEBUG_ASSERT(minuend >= subtrahend);
  return minuend - subtrahend;
}
}  // namespace

void PDFOffsetMap::markStartOfDocument(const std::shared_ptr<WriteStream>& stream) {
  fBaseOffset = stream->bytesWritten();
}

void PDFOffsetMap::markStartOfObject(int referenceNumber,
                                     const std::shared_ptr<WriteStream>& stream) {
  DEBUG_ASSERT(referenceNumber > 0);
  auto index = static_cast<size_t>(referenceNumber - 1);
  if (index >= fOffsets.size()) {
    fOffsets.resize(index + 1);
  }
  fOffsets[index] = static_cast<int>(difference(stream->bytesWritten(), fBaseOffset));
}

size_t PDFOffsetMap::objectCount() const {
  return fOffsets.size() + 1;  // Include the special zeroth object in the count.
}

int PDFOffsetMap::emitCrossReferenceTable(const std::shared_ptr<WriteStream>& stream) const {
  int xRefFileOffset = static_cast<int>(difference(stream->bytesWritten(), fBaseOffset));
  stream->writeText("xref\n0 ");
  stream->writeText(std::to_string(objectCount()));
  stream->writeText("\n0000000000 65535 f \n");
  for (int offset : fOffsets) {
    DEBUG_ASSERT(offset > 0);  // Offset was set.
    auto offsetString = std::to_string(offset);
    if (offsetString.size() < 10) {
      offsetString.insert(0, 10 - offsetString.size(), '0');
    }
    stream->writeText(offsetString);
    stream->writeText(" 00000 n \n");
  }
  return xRefFileOffset;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {
void serializeHeader(PDFOffsetMap* offsetMap, const std::shared_ptr<WriteStream>& stream) {
  offsetMap->markStartOfDocument(stream);
  constexpr std::array<uint8_t, 4> TGFX_Mark = {'T' | 0x80, 'G' | 0x80, 'F' | 0x80, 'X' | 0x80};
  stream->writeText("%PDF-1.4\n%");
  stream->write(TGFX_Mark.data(), TGFX_Mark.size());
  stream->writeText("\n");
  // The PDF specification recommends including random bytes (>128) in the header
  // to prevent the file from being misidentified as text. Here we use TGFX with high bits set.
}

// PDFIndirectReference make_srgb_color_profile(PDFDocument* doc) {
//  auto dict = PDFDictionary::Make();
//   dict->insertInt("N", 3);
//   dict->insertObject("Range", MakePDFArray(0, 1, 0, 1, 0, 1));
//   return SkPDFStreamOut(std::move(dict), SkMemoryStream::Make(SkSrgbIcm()), doc,
//                         SkPDFSteamCompressionEnabled::Yes);
// }

std::unique_ptr<PDFArray> make_srgb_output_intents(PDFDocument* /*doc*/) {
  // sRGB is specified by HTML, CSS, and SVG.
  auto outputIntent = PDFDictionary::Make("OutputIntent");
  outputIntent->insertName("S", "GTS_PDFA1");
  outputIntent->insertTextString("RegistryName", "http://www.color.org");
  outputIntent->insertTextString("OutputConditionIdentifier", "Custom");
  outputIntent->insertTextString("Info", "sRGB IEC61966-2.1");
  // outputIntent->insertRef("DestOutputProfile", make_srgb_color_profile(doc));
  auto intentArray = MakePDFArray();
  intentArray->appendObject(std::move(outputIntent));
  return intentArray;
}

PDFIndirectReference generate_page_tree(PDFDocument* doc,
                                        std::vector<std::unique_ptr<PDFDictionary>> pages,
                                        const std::vector<PDFIndirectReference>& pageRefs) {
  // PDF wants a tree describing all the pages in the document.  We arbitrary
  // choose 8 (kNodeSize) as the number of allowed children.  The internal
  // nodes have type "Pages" with an array of children, a parent pointer, and
  // the number of leaves below the node as "Count."  The leaves are passed
  // into the method, have type "Page" and need a parent pointer. This method
  // builds the tree bottom up, skipping internal nodes that would have only
  // one child.
  DEBUG_ASSERT(!pages.empty());
  struct PageTreeNode {
    std::unique_ptr<PDFDictionary> fNode;
    PDFIndirectReference fReservedRef;
    int fPageObjectDescendantCount;

    static std::vector<PageTreeNode> Layer(std::vector<PageTreeNode> vec, PDFDocument* doc) {
      std::vector<PageTreeNode> result;
      static constexpr size_t kMaxNodeSize = 8;
      const size_t n = vec.size();
      DEBUG_ASSERT(n >= 1);
      const size_t result_len = ((n - 1) / kMaxNodeSize) + 1;
      DEBUG_ASSERT(result_len >= 1);
      DEBUG_ASSERT((n == 1 || result_len < n));
      result.reserve(result_len);
      size_t index = 0;
      for (size_t i = 0; i < result_len; ++i) {
        if (n != 1 && index + 1 == n) {  // No need to create a new node.
          result.push_back(std::move(vec[index++]));
          continue;
        }
        PDFIndirectReference parent = doc->reserveRef();
        auto kids_list = MakePDFArray();
        int descendantCount = 0;
        for (size_t j = 0; j < kMaxNodeSize && index < n; ++j) {
          PageTreeNode& node = vec[index++];
          node.fNode->insertRef("Parent", parent);
          kids_list->appendRef(doc->emit(*node.fNode, node.fReservedRef));
          descendantCount += node.fPageObjectDescendantCount;
        }
        auto next = PDFDictionary::Make("Pages");
        next->insertInt("Count", descendantCount);
        next->insertObject("Kids", std::move(kids_list));
        result.push_back(PageTreeNode{std::move(next), parent, descendantCount});
      }
      return result;
    }
  };
  std::vector<PageTreeNode> currentLayer;
  currentLayer.reserve(pages.size());
  DEBUG_ASSERT(pages.size() == pageRefs.size());
  for (size_t i = 0; i < pages.size(); ++i) {
    currentLayer.push_back(PageTreeNode{std::move(pages[i]), pageRefs[i], 1});
  }
  currentLayer = PageTreeNode::Layer(std::move(currentLayer), doc);
  while (currentLayer.size() > 1) {
    currentLayer = PageTreeNode::Layer(std::move(currentLayer), doc);
  }
  DEBUG_ASSERT(currentLayer.size() == 1);
  const PageTreeNode& root = currentLayer[0];
  return doc->emit(*root.fNode, root.fReservedRef);
}

std::string ToValidUtf8String(const Data& d) {
  if (d.empty()) {
    LOGE("Not a valid string, data length is zero.");
    return "";
  }

  const char* c_str = static_cast<const char*>(d.data());
  if (c_str[d.size() - 1] != 0) {
    LOGE("Not a valid string, not null-terminated.");
    return "";
  }

  // CountUTF8 returns -1 if there's an invalid UTF-8 byte sequence.
  int valid_utf8_chars_count = UTF::CountUTF8(c_str, d.size() - 1);
  if (valid_utf8_chars_count == -1) {
    LOGE("Not a valid UTF-8 string.");
    return "";
  }

  return std::string(c_str, d.size() - 1);
}

PDFIndirectReference append_destinations(
    PDFDocument* doc, const std::vector<SkPDFNamedDestination>& namedDestinations) {
  PDFDictionary destinations;
  for (const SkPDFNamedDestination& dest : namedDestinations) {
    auto pdfDest = MakePDFArray();
    pdfDest->reserve(5);
    pdfDest->appendRef(dest.fPage);
    pdfDest->appendName("XYZ");
    pdfDest->appendScalar(dest.fPoint.x);
    pdfDest->appendScalar(dest.fPoint.y);
    pdfDest->appendInt(0);  // Leave zoom unchanged
    destinations.insertObject(ToValidUtf8String(*dest.fName), std::move(pdfDest));
  }
  return doc->emit(destinations);
}

void serialize_footer(const PDFOffsetMap& offsetMap, const std::shared_ptr<WriteStream>& stream,
                      PDFIndirectReference infoDict, PDFIndirectReference docCatalog, UUID uuid) {
  int xRefFileOffset = offsetMap.emitCrossReferenceTable(stream);
  PDFDictionary trailerDict;
  trailerDict.insertInt("Size", offsetMap.objectCount());
  DEBUG_ASSERT(docCatalog != PDFIndirectReference());
  trailerDict.insertRef("Root", docCatalog);
  DEBUG_ASSERT(infoDict != PDFIndirectReference());
  trailerDict.insertRef("Info", infoDict);
  if (UUID() != uuid) {
    trailerDict.insertObject("ID", PDFMetadataUtils::MakePDFId(uuid, uuid));
  }
  stream->writeText("trailer\n");
  trailerDict.emitObject(stream);
  stream->writeText("\nstartxref\n");
  stream->writeText(std::to_string(xRefFileOffset));
  stream->writeText("\n%%EOF\n");
}

void begin_indirect_object(PDFOffsetMap* offsetMap, PDFIndirectReference ref,
                           const std::shared_ptr<WriteStream>& stream) {
  offsetMap->markStartOfObject(ref.fValue, stream);
  stream->writeText(std::to_string(ref.fValue));
  stream->writeText(" 0 obj\n");  // Generation number is always 0.
}

void end_indirect_object(const std::shared_ptr<WriteStream>& stream) {
  stream->writeText("\nendobj\n");
}

std::vector<const PDFFont*> get_fonts(const PDFDocument& canon) {
  std::vector<const PDFFont*> fonts;
  for (const auto& [_, strike] : canon.fStrikes) {
    for (const auto& [unused, font] : strike->fFontMap) {
      fonts.push_back(font.get());
    }
  }

  // Sort so the output PDF is reproducible.
  std::sort(fonts.begin(), fonts.end(), [](const PDFFont* a, const PDFFont* b) {
    return a->indirectReference().fValue < b->indirectReference().fValue;
  });
  return fonts;
}

}  // namespace

PDFDocument::PDFDocument(std::shared_ptr<WriteStream> stream, Context* context, PDFMetadata meta)
    : Document(std::move(stream)), _context(context), _metadata(std::move(meta)) {
  constexpr float DpiForRasterScaleOne = 72.0f;
  if (_metadata.rasterDPI != DpiForRasterScaleOne) {
    fInverseRasterScale = DpiForRasterScaleOne / _metadata.rasterDPI;
    fRasterScale = _metadata.rasterDPI / DpiForRasterScaleOne;
  }
  if (_metadata.structureElementTreeRoot) {
    fTagTree.init(_metadata.structureElementTreeRoot, _metadata.outline);
  }
}

PDFDocument::~PDFDocument() {
  close();
}

std::unique_ptr<Canvas> PDFDocument::MakeCanvas(PDFExportContext* drawContext) {
  return std::unique_ptr<Canvas>(new Canvas(drawContext));
}

const Matrix& PDFDocument::currentPageTransform() const {
  // If not on a page (like when emitting a Type3 glyph) return identity.
  if (!this->hasCurrentPage()) {
    return Matrix::I();
  }
  return drawContext->initialTransform();
}

Canvas* PDFDocument::onBeginPage(float width, float height) {
  if (fPages.empty()) {
    // if this is the first page if the document.
    {
      std::lock_guard<std::mutex> autoLock(fMutex);
      serializeHeader(&fOffsetMap, stream());
    }

    fInfoDict = this->emit(*PDFMetadataUtils::MakeDocumentInformationDict(_metadata));
    if (_metadata.PDFA) {
      fUUID = PDFMetadataUtils::CreateUUID(_metadata);
      // We use the same UUID for Document ID and Instance ID since this
      // is the first revision of this document (and Skia does not
      // support revising existing PDF documents).
      // If we are not in PDF/A mode, don't use a UUID since testing
      // works best with reproducible outputs.
      fXMP = PDFMetadataUtils::MakeXMPObject(_metadata, fUUID, fUUID, this);
    }
  }

  // By scaling the page at the device level, we will create bitmap layer
  // devices at the rasterized scale, not the 72dpi scale.  Bitmap layer
  // devices are created when saveLayer is called with an ImageFilter;  see
  // SkPDFDevice::onCreateDevice().
  ISize pageSize = {static_cast<int>(std::round(width * fRasterScale)),
                    static_cast<int>(std::round(height * fRasterScale))};
  Matrix initialTransform;
  // Skia uses the top left as the origin but PDF natively has the origin at the
  // bottom left. This matrix corrects for that, as well as the raster scale.
  initialTransform.setScale(fInverseRasterScale, -fInverseRasterScale);
  initialTransform.setTranslateY(fInverseRasterScale * pageSize.height);

  // fPageDevice = sk_make_sp<SkPDFDevice>(pageSize, this, initialTransform);
  // reset_object(&fCanvas, fPageDevice);
  // fCanvas.scale(fRasterScale, fRasterScale);

  drawContext = new PDFExportContext(pageSize, this, initialTransform);
  canvas = new Canvas(drawContext);

  fPageRefs.push_back(this->reserveRef());
  // return &fCanvas;
  return canvas;
}

void PDFDocument::onEndPage() {
  // SkASSERT(!fCanvas.imageInfo().dimensions().isZero());
  // reset_object(&fCanvas);
  // SkASSERT(fPageDevice);

  auto page = PDFDictionary::Make("Page");

  auto mediaSize = ISize::Make(drawContext->pageSize().width * fInverseRasterScale,
                               drawContext->pageSize().height * fInverseRasterScale);
  auto pageContent = drawContext->getContent();

  auto resourceDict = drawContext->makeResourceDict();
  DEBUG_ASSERT(!fPageRefs.empty());

  page->insertObject("Resources", std::move(resourceDict));
  page->insertObject("MediaBox", PDFUtils::RectToArray(Rect::MakeSize(mediaSize)));

  // if (std::unique_ptr<PDFArray> annotations = getAnnotations()) {
  //   page->insertObject("Annots", std::move(annotations));
  //   fCurrentPageLinks.clear();
  // }

  auto contentStream = Stream::MakeFromData(pageContent);
  page->insertRef("Contents", PDFStreamOut(nullptr, std::move(contentStream), this));

  // The StructParents unique identifier for each page is just its
  // 0-based page index.
  page->insertInt("StructParents", static_cast<int>(this->currentPageIndex()));
  fPages.emplace_back(std::move(page));

  delete canvas;
  canvas = nullptr;
  delete drawContext;
  drawContext = nullptr;
}

void PDFDocument::onClose() {
  // SkASSERT(fCanvas.imageInfo().dimensions().isZero());
  if (fPages.empty()) {
    this->waitForJobs();
    return;
  }
  auto docCatalog = PDFDictionary::Make("Catalog");
  if (_metadata.PDFA) {
    DEBUG_ASSERT(fXMP != PDFIndirectReference());
    docCatalog->insertRef("Metadata", fXMP);
    // Don't specify OutputIntents if we are not in PDF/A mode since
    // no one has ever asked for this feature.
    docCatalog->insertObject("OutputIntents", make_srgb_output_intents(this));
  }

  docCatalog->insertRef("Pages", generate_page_tree(this, std::move(fPages), fPageRefs));

  if (!fNamedDestinations.empty()) {
    docCatalog->insertRef("Dests", append_destinations(this, fNamedDestinations));
    fNamedDestinations.clear();
  }

  // Handle tagged PDFs.
  if (PDFIndirectReference root = fTagTree.makeStructTreeRoot(this)) {
    // In the document catalog, indicate that this PDF is tagged.
    auto markInfo = PDFDictionary::Make("MarkInfo");
    markInfo->insertBool("Marked", true);
    docCatalog->insertObject("MarkInfo", std::move(markInfo));
    docCatalog->insertRef("StructTreeRoot", root);

    if (PDFIndirectReference outline = fTagTree.makeOutline(this)) {
      docCatalog->insertRef("Outlines", outline);
    }
  }

  // If ViewerPreferences DisplayDocTitle isn't set to true, accessibility checks will fail.
  if (!_metadata.title.empty()) {
    auto viewerPrefs = PDFDictionary::Make("ViewerPreferences");
    viewerPrefs->insertBool("DisplayDocTitle", true);
    docCatalog->insertObject("ViewerPreferences", std::move(viewerPrefs));
  }

  auto lang = _metadata.lang;
  if (lang.empty()) {
    lang = fTagTree.getRootLanguage();
  }
  if (!lang.empty()) {
    docCatalog->insertTextString("Lang", lang);
  }

  auto docCatalogRef = this->emit(*docCatalog);

  for (const auto* f : get_fonts(*this)) {
    f->emitSubset(this);
  }

  this->waitForJobs();
  {
    std::lock_guard<std::mutex> autoLock(fMutex);
    serialize_footer(fOffsetMap, stream(), fInfoDict, docCatalogRef, fUUID);
  }
}

void PDFDocument::onAbort() {
}

void PDFDocument::waitForJobs() {
  // fJobCount can increase while we wait.
  // while (fJobCount > 0) {
  //   fSemaphore.wait();
  //   --fJobCount;
  // }
}

std::string PDFDocument::nextFontSubsetTag() {
  // PDF 32000-1:2008 Section 9.6.4 FontSubsets "The tag shall consist of six uppercase letters"
  // "followed by a plus sign" "different subsets in the same PDF file shall have different tags."
  // There are 26^6 or 308,915,776 possible values. So start in range then increment and mod.
  uint32_t thisFontSubsetTag = fNextFontSubsetTag;
  fNextFontSubsetTag = (fNextFontSubsetTag + 1u) % 308915776u;

  std::string subsetTag(7, ' ');
  char* subsetTagData = subsetTag.data();
  for (size_t i = 0; i < 6; ++i) {
    subsetTagData[i] = 'A' + (thisFontSubsetTag % 26);
    thisFontSubsetTag /= 26;
  }
  subsetTagData[6] = '+';
  return subsetTag;
}

PDFIndirectReference PDFDocument::emit(const PDFObject& object, PDFIndirectReference ref) {
  std::lock_guard<std::mutex> autoLock(fMutex);
  object.emitObject(this->beginObject(ref));
  this->endObject();
  return ref;
}

std::shared_ptr<WriteStream> PDFDocument::beginObject(PDFIndirectReference ref) {
  begin_indirect_object(&fOffsetMap, ref, stream());
  return stream();
}

void PDFDocument::endObject() {
  end_indirect_object(stream());
}

PDFIndirectReference PDFDocument::getPage(size_t pageIndex) const {
  DEBUG_ASSERT(pageIndex < fPageRefs.size());
  return fPageRefs[pageIndex];
}

}  // namespace tgfx