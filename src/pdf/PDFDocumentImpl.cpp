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

#include "PDFDocumentImpl.h"
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

std::shared_ptr<PDFDocument> PDFDocument::Make(std::shared_ptr<WriteStream> stream,
                                               Context* context, PDFMetadata metadata,
                                               std::shared_ptr<ColorSpace> colorSpace) {
  if (!stream || !context) {
    return nullptr;
  }
  if (metadata.rasterDPI <= 0) {
    metadata.rasterDPI = 72.0f;
  }
  metadata.encodingQuality = std::max(metadata.encodingQuality, 0);
  return std::make_shared<PDFDocumentImpl>(stream, context, metadata, std::move(colorSpace));
}

namespace {
size_t Difference(size_t minuend, size_t subtrahend) {
  DEBUG_ASSERT(minuend >= subtrahend);
  return minuend - subtrahend;
}
}  // namespace

void PDFOffsetMap::markStartOfDocument(const std::shared_ptr<WriteStream>& stream) {
  baseOffset = stream->bytesWritten();
}

void PDFOffsetMap::markStartOfObject(int referenceNumber,
                                     const std::shared_ptr<WriteStream>& stream) {
  DEBUG_ASSERT(referenceNumber > 0);
  auto index = static_cast<size_t>(referenceNumber - 1);
  if (index >= offsets.size()) {
    offsets.resize(index + 1);
  }
  offsets[index] = static_cast<int>(Difference(stream->bytesWritten(), baseOffset));
}

size_t PDFOffsetMap::objectCount() const {
  return offsets.size() + 1;  // Include the special zeroth object in the count.
}

int PDFOffsetMap::emitCrossReferenceTable(const std::shared_ptr<WriteStream>& stream) const {
  int xRefFileOffset = static_cast<int>(Difference(stream->bytesWritten(), baseOffset));
  stream->writeText("xref\n0 ");
  stream->writeText(std::to_string(objectCount()));
  stream->writeText("\n0000000000 65535 f \n");
  for (int offset : offsets) {
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
void SerializeHeader(PDFOffsetMap* offsetMap, const std::shared_ptr<WriteStream>& stream) {
  offsetMap->markStartOfDocument(stream);
  constexpr std::array<uint8_t, 4> TGFX_Mark = {'T' | 0x80, 'G' | 0x80, 'F' | 0x80, 'X' | 0x80};
  stream->writeText("%PDF-1.4\n%");
  stream->write(TGFX_Mark.data(), TGFX_Mark.size());
  stream->writeText("\n");
  // The PDF specification recommends including random bytes (>128) in the header
  // to prevent the file from being misidentified as text. Here we use TGFX with high bits set.
}

std::unique_ptr<PDFArray> make_srgb_output_intents(PDFDocumentImpl* /*doc*/) {
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

PDFIndirectReference generate_page_tree(PDFDocumentImpl* doc,
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

    static std::vector<PageTreeNode> Layer(std::vector<PageTreeNode> vec, PDFDocumentImpl* doc) {
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
    PDFDocumentImpl* doc, const std::vector<PDFNamedDestination>& namedDestinations) {
  PDFDictionary destinations;
  for (const PDFNamedDestination& dest : namedDestinations) {
    auto pdfDest = MakePDFArray();
    pdfDest->reserve(5);
    pdfDest->appendRef(dest.page);
    pdfDest->appendName("XYZ");
    pdfDest->appendScalar(dest.point.x);
    pdfDest->appendScalar(dest.point.y);
    pdfDest->appendInt(0);  // Leave zoom unchanged
    destinations.insertObject(ToValidUtf8String(*dest.name), std::move(pdfDest));
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
  offsetMap->markStartOfObject(ref.value, stream);
  stream->writeText(std::to_string(ref.value));
  stream->writeText(" 0 obj\n");  // Generation number is always 0.
}

void end_indirect_object(const std::shared_ptr<WriteStream>& stream) {
  stream->writeText("\nendobj\n");
}

std::vector<const PDFFont*> get_fonts(const PDFDocumentImpl& canon) {
  std::vector<const PDFFont*> fonts;
  for (const auto& [_, strike] : canon.strikes) {
    for (const auto& [unused, font] : strike->fontMap) {
      fonts.push_back(font.get());
    }
  }

  // Sort so the output PDF is reproducible.
  std::sort(fonts.begin(), fonts.end(), [](const PDFFont* a, const PDFFont* b) {
    return a->indirectReference().value < b->indirectReference().value;
  });
  return fonts;
}

}  // namespace

PDFDocumentImpl::PDFDocumentImpl(std::shared_ptr<WriteStream> stream, Context* context,
                                 PDFMetadata meta, std::shared_ptr<ColorSpace> colorSpace)
    : _stream(std::move(stream)), _context(context), _metadata(std::move(meta)),
      _colorSpace(std::move(colorSpace)) {
  if (_metadata.rasterDPI != ScalarDefaultRasterDPI) {
    inverseRasterScale = ScalarDefaultRasterDPI / _metadata.rasterDPI;
    rasterScale = _metadata.rasterDPI / ScalarDefaultRasterDPI;
  }
  if (_metadata.structureElementTreeRoot) {
    tagTree.init(_metadata.structureElementTreeRoot, _metadata.outline);
  }
}

PDFDocumentImpl::~PDFDocumentImpl() {
  close();
}

std::unique_ptr<Canvas> PDFDocumentImpl::MakeCanvas(PDFExportContext* drawContext) {
  return std::unique_ptr<Canvas>(new Canvas(drawContext));
}

const Matrix& PDFDocumentImpl::currentPageTransform() const {
  // If not on a page (like when emitting a Type3 glyph) return identity.
  if (!this->hasCurrentPage()) {
    return Matrix::I();
  }
  return drawContext->initialTransform();
}

Canvas* PDFDocumentImpl::beginPage(float pageWidth, float pageHeight, const Rect* contentRect) {
  if (pageWidth <= 0 || pageHeight <= 0 || state == State::Closed) {
    return nullptr;
  }
  if (state == State::InPage) {
    endPage();
  }
  auto canvas = onBeginPage(pageWidth, pageHeight);
  state = State::InPage;
  if (canvas && contentRect) {
    Rect rect = *contentRect;
    if (!rect.intersect({0, 0, pageWidth, pageWidth})) {
      return nullptr;
    }
    canvas->clipRect(rect);
    canvas->translate(rect.x(), rect.y());
  }
  return canvas;
}

void PDFDocumentImpl::endPage() {
  if (state == State::InPage) {
    onEndPage();
    state = State::BetweenPages;
  }
}

void PDFDocumentImpl::close() {
  while (true) {
    switch (state) {
      case State::BetweenPages:
        onClose();
        state = State::Closed;
        return;
      case State::InPage:
        endPage();
        break;
      case State::Closed:
        return;
    }
  }
}

void PDFDocumentImpl::abort() {
  if (state != State::Closed) {
    onAbort();
    state = State::Closed;
  }
}

Canvas* PDFDocumentImpl::onBeginPage(float width, float height) {
  if (pages.empty()) {
    // if this is the first page if the document.
    SerializeHeader(&offsetMap, _stream);
    infoDictionary = this->emit(*PDFMetadataUtils::MakeDocumentInformationDict(_metadata));
    _colorSpaceRef = emitColorSpace();
    if (_metadata.PDFA) {
      documentUUID = PDFMetadataUtils::CreateUUID(_metadata);
      // We use the same UUID for Document ID and Instance ID since this
      // is the first revision of this document.
      // If we are not in PDF/A mode, don't use a UUID since testing
      // works best with reproducible outputs.
      documentXMP = PDFMetadataUtils::MakeXMPObject(_metadata, documentUUID, documentUUID, this);
    }
  }

  // By scaling the page at the device level, we will create bitmap layer
  // devices at the rasterized scale, not the 72dpi scale.  Bitmap layer
  // devices are created when saveLayer is called with an ImageFilter;  see
  ISize pageSize = {static_cast<int>(std::round(width * rasterScale)),
                    static_cast<int>(std::round(height * rasterScale))};
  Matrix initialTransform;
  initialTransform.setScale(inverseRasterScale, -inverseRasterScale);
  initialTransform.setTranslateY(inverseRasterScale * static_cast<float>(pageSize.height));

  drawContext = new PDFExportContext(pageSize, this, initialTransform);
  _canvas = new Canvas(drawContext);

  pageRefs.push_back(this->reserveRef());
  // return &fCanvas;
  return _canvas;
}

void PDFDocumentImpl::onEndPage() {
  auto page = PDFDictionary::Make("Page");

  auto mediaSize =
      ISize::Make(static_cast<float>(drawContext->pageSize().width) * inverseRasterScale,
                  static_cast<float>(drawContext->pageSize().height) * inverseRasterScale);
  auto pageContent = drawContext->getContent();

  auto resourceDict = drawContext->makeResourceDict();
  auto dic = std::make_unique<PDFDictionary>();
  dic->insertRef("CS", _colorSpaceRef);
  resourceDict->insertObject("ColorSpace", std::move(dic));
  DEBUG_ASSERT(!pageRefs.empty());

  page->insertObject("Resources", std::move(resourceDict));
  page->insertObject("MediaBox", PDFUtils::RectToArray(Rect::MakeSize(mediaSize)));

  auto contentStream = Stream::MakeFromData(pageContent);
  page->insertRef("Contents", PDFStreamOut(nullptr, std::move(contentStream), this));

  // The StructParents unique identifier for each page is just its
  // 0-based page index.
  page->insertInt("StructParents", static_cast<int>(this->currentPageIndex()));
  pages.emplace_back(std::move(page));

  delete _canvas;
  _canvas = nullptr;
  delete drawContext;
  drawContext = nullptr;
}

void PDFDocumentImpl::onClose() {
  if (pages.empty()) {
    return;
  }
  auto docCatalog = PDFDictionary::Make("Catalog");
  if (_metadata.PDFA) {
    DEBUG_ASSERT(documentXMP != PDFIndirectReference());
    docCatalog->insertRef("Metadata", documentXMP);
    // Don't specify OutputIntents if we are not in PDF/A mode since
    // no one has ever asked for this feature.
    docCatalog->insertObject("OutputIntents", make_srgb_output_intents(this));
  }

  docCatalog->insertRef("Pages", generate_page_tree(this, std::move(pages), pageRefs));

  if (!namedDestinations.empty()) {
    docCatalog->insertRef("Dests", append_destinations(this, namedDestinations));
    namedDestinations.clear();
  }

  // Handle tagged PDFs.
  if (PDFIndirectReference root = tagTree.makeStructTreeRoot(this)) {
    // In the document catalog, indicate that this PDF is tagged.
    auto markInfo = PDFDictionary::Make("MarkInfo");
    markInfo->insertBool("Marked", true);
    docCatalog->insertObject("MarkInfo", std::move(markInfo));
    docCatalog->insertRef("StructTreeRoot", root);

    if (PDFIndirectReference outline = tagTree.makeOutline(this)) {
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
    lang = tagTree.getRootLanguage();
  }
  if (!lang.empty()) {
    docCatalog->insertTextString("Lang", lang);
  }

  auto docCatalogRef = this->emit(*docCatalog);

  for (const auto f : get_fonts(*this)) {
    f->emitSubset(this);
  }
  serialize_footer(offsetMap, _stream, infoDictionary, docCatalogRef, documentUUID);
}

void PDFDocumentImpl::onAbort() {
}

std::string PDFDocumentImpl::nextFontSubsetTag() {
  // PDF 32000-1:2008 Section 9.6.4 FontSubsets "The tag shall consist of six uppercase letters"
  // "followed by a plus sign" "different subsets in the same PDF file shall have different tags."
  // There are 26^6 or 308,915,776 possible values. So start in range then increment and mod.
  uint32_t thisFontSubsetTag = nextFontSubsetTag_;
  nextFontSubsetTag_ = (nextFontSubsetTag_ + 1u) % 308915776u;

  std::string subsetTag(7, ' ');
  char* subsetTagData = subsetTag.data();
  for (size_t i = 0; i < 6; ++i) {
    subsetTagData[i] = 'A' + (thisFontSubsetTag % 26);
    thisFontSubsetTag /= 26;
  }
  subsetTagData[6] = '+';
  return subsetTag;
}

PDFIndirectReference PDFDocumentImpl::emit(const PDFObject& object, PDFIndirectReference ref) {
  object.emitObject(this->beginObject(ref));
  this->endObject();
  return ref;
}

std::shared_ptr<WriteStream> PDFDocumentImpl::beginObject(PDFIndirectReference ref) {
  begin_indirect_object(&offsetMap, ref, _stream);
  return _stream;
}

void PDFDocumentImpl::endObject() {
  end_indirect_object(_stream);
}

PDFIndirectReference PDFDocumentImpl::emitColorSpace() {
  auto dictionary = std::make_unique<PDFDictionary>();
  dictionary->insertInt("N", 3);
  dictionary->insertName("Alternate", "DeviceRGB");
  auto iccProfile = _colorSpace->toICCProfile();
  auto stream = Stream::MakeFromData(iccProfile);
  auto ref = PDFStreamOut(std::move(dictionary), std::move(stream), this);
  PDFArray array{};
  array.appendName("ICCBased");
  array.appendRef(ref);
  return this->emit(array);
}

PDFIndirectReference PDFDocumentImpl::getPage(size_t pageIndex) const {
  DEBUG_ASSERT(pageIndex < pageRefs.size());
  return pageRefs[pageIndex];
}

}  // namespace tgfx