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

#include "PDFTag.h"
#include "core/utils/Log.h"
#include "pdf/PDFDocument.h"
#include "pdf/PDFTypes.h"

constexpr int FirstAnnotationStructParentKey = 100000;

namespace tgfx {

SkPDFTagTree::SkPDFTagTree() = default;

SkPDFTagTree::~SkPDFTagTree() = default;

// static
void SkPDFTagTree::Copy(PDFStructureElementNode& node, PDFTagNode* dst,
                        std::unordered_map<int, PDFTagNode*>* nodeMap, bool wantTitle) {
  nodeMap->insert({node.nodeId, dst});
  for (int nodeId : node.additionalNodeIds) {
    DEBUG_ASSERT(nodeMap->find(nodeId) == nodeMap->end());
    nodeMap->insert({nodeId, dst});
  }
  dst->fNodeId = node.nodeId;

  // Accumulate title text, need to be in sync with create_outline_from_headers
  const char* type = node.typeString.c_str();
  wantTitle |= fOutline == PDFMetadata::Outline::StructureElementHeaders && type[0] == 'H' &&
               '1' <= type[1] && type[1] <= '6';
  dst->fWantTitle = wantTitle;

  dst->fTypeString = node.typeString;
  dst->fAlt = node.alt;
  dst->fLang = node.lang;

  size_t childCount = node.children.size();
  auto children = std::make_unique<std::vector<PDFTagNode>>(childCount);
  for (size_t i = 0; i < childCount; ++i) {
    Copy(*node.children[i], &(*children)[i], nodeMap, wantTitle);
  }
  dst->fChildren = std::move(children);
  dst->fAttributes = std::move(node.attributes.fAttrs);
}

void SkPDFTagTree::init(PDFStructureElementNode* node, PDFMetadata::Outline outline) {
  if (node) {
    fRoot = std::make_unique<PDFTagNode>();
    fOutline = outline;
    Copy(*node, fRoot.get(), &fNodeMap, false);
  }
}

int SkPDFTagTree::Mark::id() {
  return fNode ? fNode->fMarkedContent[fMarkIndex].fMarkId : -1;
}

Point& SkPDFTagTree::Mark::point() {
  return fNode->fMarkedContent[fMarkIndex].fLocation.point;
}

auto SkPDFTagTree::createMarkIdForNodeId(int nodeId, unsigned pageIndex, Point point) -> Mark {
  if (!fRoot) {
    return Mark();
  }
  auto iter = fNodeMap.find(nodeId);
  if (iter == fNodeMap.end()) {
    return Mark();
  }
  PDFTagNode* tag = iter->second;
  DEBUG_ASSERT(tag);
  fMarksPerPage.resize(std::max(static_cast<uint32_t>(fMarksPerPage.size()), pageIndex + 1));
  auto pageMarks = fMarksPerPage[pageIndex];
  auto markID = pageMarks.size();
  tag->fMarkedContent.push_back({{point, pageIndex}, static_cast<int>(markID)});
  pageMarks.push_back(tag);
  return Mark(tag, tag->fMarkedContent.size() - 1);
}

int SkPDFTagTree::createStructParentKeyForNodeId(int nodeId, unsigned) {
  if (!fRoot) {
    return -1;
  }
  auto iter = fNodeMap.find(nodeId);
  if (iter == fNodeMap.end()) {
    return -1;
  }
  PDFTagNode* tag = iter->second;
  DEBUG_ASSERT(tag);

  tag->fCanDiscard = PDFTagNode::State::kNo;

  int nextStructParentKey =
      FirstAnnotationStructParentKey + static_cast<int>(fParentTreeAnnotationNodeIds.size());
  fParentTreeAnnotationNodeIds.push_back(nodeId);
  return nextStructParentKey;
}

static bool can_discard(PDFTagNode* node) {
  if (node->fCanDiscard == PDFTagNode::State::kYes) {
    return true;
  }
  if (node->fCanDiscard == PDFTagNode::State::kNo) {
    return false;
  }
  if (!node->fMarkedContent.empty()) {
    node->fCanDiscard = PDFTagNode::State::kNo;
    return false;
  }
  for (size_t i = 0; i < node->fChildren->size(); ++i) {
    auto* child = &(*node->fChildren)[i];
    if (!can_discard(child)) {
      node->fCanDiscard = PDFTagNode::State::kNo;
      return false;
    }
  }
  node->fCanDiscard = PDFTagNode::State::kYes;
  return true;
}

PDFIndirectReference SkPDFTagTree::PrepareTagTreeToEmit(PDFIndirectReference parent,
                                                        PDFTagNode* node, PDFDocument* doc) {
  PDFIndirectReference ref = doc->reserveRef();
  std::unique_ptr<PDFArray> kids = MakePDFArray();
  const auto& children = node->fChildren;
  for (size_t i = 0; i < children->size(); ++i) {
    auto* child = &(*children)[i];
    if (!(can_discard(child))) {
      kids->appendRef(PrepareTagTreeToEmit(ref, child, doc));
    }
  }
  for (const PDFTagNode::MarkedContentInfo& info : node->fMarkedContent) {
    std::unique_ptr<PDFDictionary> mcr = PDFDictionary::Make("MCR");
    mcr->insertRef("Pg", doc->getPage(info.fLocation.pageIndex));
    mcr->insertInt("MCID", info.fMarkId);
    kids->appendObject(std::move(mcr));
  }
  for (const PDFTagNode::AnnotationInfo& annotationInfo : node->fAnnotations) {
    std::unique_ptr<PDFDictionary> annotationDict = PDFDictionary::Make("OBJR");
    annotationDict->insertRef("Obj", annotationInfo.fAnnotationRef);
    annotationDict->insertRef("Pg", doc->getPage(annotationInfo.fPageIndex));
    kids->appendObject(std::move(annotationDict));
  }
  node->fRef = ref;
  auto dict = PDFDictionary::Make("StructElem");
  dict->insertName("S", node->fTypeString.empty() ? "NonStruct" : node->fTypeString.c_str());
  if (!node->fAlt.empty()) {
    dict->insertTextString("Alt", node->fAlt);
  }
  if (!node->fLang.empty()) {
    dict->insertTextString("Lang", node->fLang);
  }
  dict->insertRef("P", parent);
  dict->insertObject("K", std::move(kids));
  if (node->fAttributes) {
    dict->insertObject("A", std::move(node->fAttributes));
  }

  // Each node has a unique ID that also needs to be referenced
  // in a separate IDTree node, along with the lowest and highest
  // unique ID string.
  auto idString = PDFTagNode::nodeIdToString(node->fNodeId);
  dict->insertByteString("ID", idString.c_str());
  IDTreeEntry idTreeEntry = {node->fNodeId, ref};
  fIdTreeEntries.push_back(idTreeEntry);

  return doc->emit(*dict, ref);
}

void SkPDFTagTree::addNodeAnnotation(int nodeId, PDFIndirectReference annotationRef,
                                     unsigned pageIndex) {
  if (!fRoot) {
    return;
  }
  auto iter = fNodeMap.find(nodeId);
  if (iter == fNodeMap.end()) {
    return;
  }
  auto* tag = iter->second;
  DEBUG_ASSERT(tag);

  PDFTagNode::AnnotationInfo annotationInfo = {pageIndex, annotationRef};
  tag->fAnnotations.push_back(annotationInfo);
}

void SkPDFTagTree::addNodeTitle(int nodeId, const std::vector<char>& title) {
  if (!fRoot) {
    return;
  }
  auto iter = fNodeMap.find(nodeId);
  if (iter == fNodeMap.end()) {
    return;
  }
  auto* tag = iter->second;
  DEBUG_ASSERT(tag);

  if (tag->fWantTitle) {
    tag->fTitle.append(title.data(), title.size());
    // Arbitrary cutoff for size.
    if (tag->fTitle.size() > 1023) {
      tag->fWantTitle = false;
    }
  }
}

PDFIndirectReference SkPDFTagTree::makeStructTreeRoot(PDFDocument* doc) {
  if (!fRoot || can_discard(fRoot.get())) {
    return PDFIndirectReference();
  }

  PDFIndirectReference ref = doc->reserveRef();

  auto pageCount = static_cast<uint32_t>(doc->pageCount());

  // Build the StructTreeRoot.
  PDFDictionary structTreeRoot("StructTreeRoot");
  structTreeRoot.insertRef("K", PrepareTagTreeToEmit(ref, fRoot.get(), doc));
  structTreeRoot.insertInt("ParentTreeNextKey", static_cast<int>(pageCount));

  // Build the parent tree, which consists of two things:
  // (1) For each page, a mapping from the marked content IDs on
  // each page to their corresponding tags
  // (2) For each annotation, an indirect reference to that
  // annotation's struct tree element.
  PDFDictionary parentTree("ParentTree");
  auto parentTreeNums = MakePDFArray();

  // First, one entry per page.
  DEBUG_ASSERT(static_cast<uint32_t>(fMarksPerPage.size()) <= pageCount);
  for (size_t j = 0; j < fMarksPerPage.size(); ++j) {
    auto pageMarks = fMarksPerPage[j];
    PDFArray markToTagArray;
    for (PDFTagNode* mark : pageMarks) {
      DEBUG_ASSERT(mark->fRef);
      markToTagArray.appendRef(mark->fRef);
    }
    parentTreeNums->appendInt(static_cast<int>(j));
    parentTreeNums->appendRef(doc->emit(markToTagArray));
  }

  // Then, one entry per annotation.
  for (size_t j = 0; j < fParentTreeAnnotationNodeIds.size(); ++j) {
    int nodeId = fParentTreeAnnotationNodeIds[j];
    int structParentKey = FirstAnnotationStructParentKey + static_cast<int>(j);

    auto iter = fNodeMap.find(nodeId);
    if (iter == fNodeMap.end()) {
      continue;
    }
    auto* tag = iter->second;
    parentTreeNums->appendInt(structParentKey);
    parentTreeNums->appendRef(tag->fRef);
  }

  parentTree.insertObject("Nums", std::move(parentTreeNums));
  structTreeRoot.insertRef("ParentTree", doc->emit(parentTree));

  // Build the IDTree, a mapping from every unique ID string to
  // a reference to its corresponding structure element node.
  if (!fIdTreeEntries.empty()) {
    std::sort(fIdTreeEntries.begin(), fIdTreeEntries.end(),
              [](const IDTreeEntry& a, const IDTreeEntry& b) { return a.nodeId < b.nodeId; });

    PDFDictionary idTree;
    PDFDictionary idTreeLeaf;
    auto limits = MakePDFArray();
    auto lowestNodeIdString = PDFTagNode::nodeIdToString(fIdTreeEntries.begin()->nodeId);
    limits->appendByteString(lowestNodeIdString);
    auto highestNodeIdString = PDFTagNode::nodeIdToString(fIdTreeEntries.rbegin()->nodeId);
    limits->appendByteString(highestNodeIdString);
    idTreeLeaf.insertObject("Limits", std::move(limits));
    auto names = MakePDFArray();
    for (const IDTreeEntry& entry : fIdTreeEntries) {
      auto idString = PDFTagNode::nodeIdToString(entry.nodeId);
      names->appendByteString(idString);
      names->appendRef(entry.ref);
    }
    idTreeLeaf.insertObject("Names", std::move(names));
    auto idTreeKids = MakePDFArray();
    idTreeKids->appendRef(doc->emit(idTreeLeaf));
    idTree.insertObject("Kids", std::move(idTreeKids));
    structTreeRoot.insertRef("IDTree", doc->emit(idTree));
  }

  return doc->emit(structTreeRoot, ref);
}

namespace {
struct OutlineEntry {
  struct Content {
    std::string fText;
    Location fLocation;
    void accumulate(Content const& child) {
      fText += child.fText;
      fLocation.accumulate(child.fLocation);
    }
  };

  Content fContent;
  int fHeaderLevel;
  PDFIndirectReference fRef;
  PDFIndirectReference fStructureRef;
  std::vector<OutlineEntry> fChildren = {};
  size_t fDescendentsEmitted = 0;

  void emitDescendents(PDFDocument* const doc) {
    fDescendentsEmitted = fChildren.size();
    for (size_t i = 0; i < fChildren.size(); ++i) {
      auto&& child = fChildren[i];
      child.emitDescendents(doc);
      fDescendentsEmitted += child.fDescendentsEmitted;

      PDFDictionary entry;
      entry.insertTextString("Title", child.fContent.fText);

      auto destination = MakePDFArray();
      destination->appendRef(doc->getPage(child.fContent.fLocation.pageIndex));
      destination->appendName("XYZ");
      destination->appendScalar(child.fContent.fLocation.point.x);
      destination->appendScalar(child.fContent.fLocation.point.y);
      destination->appendInt(0);
      entry.insertObject("Dest", std::move(destination));

      entry.insertRef("Parent", child.fRef);
      if (child.fStructureRef) {
        entry.insertRef("SE", child.fStructureRef);
      }
      if (0 < i) {
        entry.insertRef("Prev", fChildren[i - 1].fRef);
      }
      if (i < fChildren.size() - 1) {
        entry.insertRef("Next", fChildren[i + 1].fRef);
      }
      if (!child.fChildren.empty()) {
        entry.insertRef("First", child.fChildren.front().fRef);
        entry.insertRef("Last", child.fChildren.back().fRef);
        entry.insertInt("Count", child.fDescendentsEmitted);
      }
      doc->emit(entry, child.fRef);
    }
  }
};

OutlineEntry::Content create_outline_entry_content(PDFTagNode* const node) {
  std::string text;
  if (!node->fTitle.empty()) {
    text = node->fTitle;
  } else if (!node->fAlt.empty()) {
    text = node->fAlt;
  }

  // The uppermost/leftmost point on the earliest page of this node's marks.
  Location markPoint;
  for (auto&& mark : node->fMarkedContent) {
    markPoint.accumulate(mark.fLocation);
  }

  OutlineEntry::Content content{std::move(text), std::move(markPoint)};

  // Accumulate children
  const auto& children = node->fChildren;
  for (auto&& child : *children) {
    if (can_discard(&child)) {
      continue;
    }
    content.accumulate(create_outline_entry_content(&child));
  }
  return content;
}
void create_outline_from_headers(PDFDocument* const doc, PDFTagNode* const node,
                                 std::vector<OutlineEntry*>& stack) {
  char const* type = node->fTypeString.c_str();
  if (type[0] == 'H' && '1' <= type[1] && type[1] <= '6') {
    int level = type[1] - '0';
    while (level <= stack.back()->fHeaderLevel) {
      stack.pop_back();
    }
    OutlineEntry::Content content = create_outline_entry_content(node);
    if (!content.fText.empty()) {
      OutlineEntry e{std::move(content), level, doc->reserveRef(), node->fRef};
      stack.push_back(&stack.back()->fChildren.emplace_back(std::move(e)));
      return;
    }
  }

  const auto& children = node->fChildren;
  for (auto&& child : *children) {
    if (can_discard(&child)) {
      continue;
    }
    create_outline_from_headers(doc, &child, stack);
  }
}

}  // namespace

PDFIndirectReference SkPDFTagTree::makeOutline(PDFDocument* doc) {
  if (!fRoot || can_discard(fRoot.get()) ||
      fOutline != PDFMetadata::Outline::StructureElementHeaders) {
    return PDFIndirectReference();
  }

  std::vector<OutlineEntry*> stack;
  OutlineEntry top{{"", Location()}, 0, {}, {}};
  stack.push_back(&top);
  create_outline_from_headers(doc, fRoot.get(), stack);
  if (top.fChildren.empty()) {
    return PDFIndirectReference();
  }
  top.emitDescendents(doc);
  auto outlineRef = doc->reserveRef();
  PDFDictionary outline("Outlines");
  outline.insertRef("First", top.fChildren.front().fRef);
  outline.insertRef("Last", top.fChildren.back().fRef);
  outline.insertInt("Count", top.fDescendentsEmitted);

  return doc->emit(outline, outlineRef);
}

std::string SkPDFTagTree::getRootLanguage() {
  return fRoot ? fRoot->fLang : "";
}

}  // namespace tgfx