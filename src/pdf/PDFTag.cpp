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

PDFTagTree::PDFTagTree() = default;

PDFTagTree::~PDFTagTree() = default;

// static
void PDFTagTree::Copy(PDFStructureElementNode& node, PDFTagNode* dst,
                      std::unordered_map<int, PDFTagNode*>* nodeMap, bool wantTitle) {
  nodeMap->insert({node.nodeId, dst});
  for (int nodeId : node.additionalNodeIds) {
    DEBUG_ASSERT(nodeMap->find(nodeId) == nodeMap->end());
    nodeMap->insert({nodeId, dst});
  }
  dst->nodeId = node.nodeId;

  // Accumulate title text, need to be in sync with create_outline_from_headers
  const char* type = node.typeString.c_str();
  wantTitle |= outline == PDFMetadata::Outline::StructureElementHeaders && type[0] == 'H' &&
               '1' <= type[1] && type[1] <= '6';
  dst->wantTitle = wantTitle;

  dst->typeString = node.typeString;
  dst->alt = node.alt;
  dst->lang = node.lang;

  size_t childCount = node.children.size();
  auto children = std::make_unique<std::vector<PDFTagNode>>(childCount);
  for (size_t i = 0; i < childCount; ++i) {
    Copy(*node.children[i], &(*children)[i], nodeMap, wantTitle);
  }
  dst->children = std::move(children);
  dst->attributes = std::move(node.attributes.attrs);
}

void PDFTagTree::init(PDFStructureElementNode* node, PDFMetadata::Outline inputOutline) {
  if (node) {
    root = std::make_unique<PDFTagNode>();
    outline = inputOutline;
    Copy(*node, root.get(), &nodeMap, false);
  }
}

int PDFTagTree::Mark::id() {
  return node ? node->markedContent[markIndex].markId : -1;
}

Point& PDFTagTree::Mark::point() {
  return node->markedContent[markIndex].location.point;
}

auto PDFTagTree::createMarkIdForNodeId(int nodeId, unsigned pageIndex, Point point) -> Mark {
  if (!root) {
    return Mark();
  }
  auto iter = nodeMap.find(nodeId);
  if (iter == nodeMap.end()) {
    return Mark();
  }
  PDFTagNode* tag = iter->second;
  DEBUG_ASSERT(tag);
  marksPerPage.resize(std::max(static_cast<uint32_t>(marksPerPage.size()), pageIndex + 1));
  auto pageMarks = marksPerPage[pageIndex];
  auto markID = pageMarks.size();
  tag->markedContent.push_back({{point, pageIndex}, static_cast<int>(markID)});
  pageMarks.push_back(tag);
  return Mark(tag, tag->markedContent.size() - 1);
}

int PDFTagTree::createStructParentKeyForNodeId(int nodeId, unsigned) {
  if (!root) {
    return -1;
  }
  auto iter = nodeMap.find(nodeId);
  if (iter == nodeMap.end()) {
    return -1;
  }
  PDFTagNode* tag = iter->second;
  DEBUG_ASSERT(tag);

  tag->canDiscard = PDFTagNode::State::No;

  int nextStructParentKey =
      FirstAnnotationStructParentKey + static_cast<int>(parentTreeAnnotationNodeIds.size());
  parentTreeAnnotationNodeIds.push_back(nodeId);
  return nextStructParentKey;
}

static bool can_discard(PDFTagNode* node) {
  if (node->canDiscard == PDFTagNode::State::Yes) {
    return true;
  }
  if (node->canDiscard == PDFTagNode::State::No) {
    return false;
  }
  if (!node->markedContent.empty()) {
    node->canDiscard = PDFTagNode::State::No;
    return false;
  }
  for (size_t i = 0; i < node->children->size(); ++i) {
    auto* child = &(*node->children)[i];
    if (!can_discard(child)) {
      node->canDiscard = PDFTagNode::State::No;
      return false;
    }
  }
  node->canDiscard = PDFTagNode::State::Yes;
  return true;
}

PDFIndirectReference PDFTagTree::PrepareTagTreeToEmit(PDFIndirectReference parent, PDFTagNode* node,
                                                      PDFDocument* doc) {
  PDFIndirectReference ref = doc->reserveRef();
  std::unique_ptr<PDFArray> kids = MakePDFArray();
  const auto& children = node->children;
  for (size_t i = 0; i < children->size(); ++i) {
    auto* child = &(*children)[i];
    if (!(can_discard(child))) {
      kids->appendRef(PrepareTagTreeToEmit(ref, child, doc));
    }
  }
  for (const PDFTagNode::MarkedContentInfo& info : node->markedContent) {
    std::unique_ptr<PDFDictionary> mcr = PDFDictionary::Make("MCR");
    mcr->insertRef("Pg", doc->getPage(info.location.pageIndex));
    mcr->insertInt("MCID", info.markId);
    kids->appendObject(std::move(mcr));
  }
  for (const PDFTagNode::AnnotationInfo& annotationInfo : node->annotations) {
    std::unique_ptr<PDFDictionary> annotationDict = PDFDictionary::Make("OBJR");
    annotationDict->insertRef("Obj", annotationInfo.annotationRef);
    annotationDict->insertRef("Pg", doc->getPage(annotationInfo.pageIndex));
    kids->appendObject(std::move(annotationDict));
  }
  node->ref = ref;
  auto dict = PDFDictionary::Make("StructElem");
  dict->insertName("S", node->typeString.empty() ? "NonStruct" : node->typeString.c_str());
  if (!node->alt.empty()) {
    dict->insertTextString("Alt", node->alt);
  }
  if (!node->lang.empty()) {
    dict->insertTextString("Lang", node->lang);
  }
  dict->insertRef("P", parent);
  dict->insertObject("K", std::move(kids));
  if (node->attributes) {
    dict->insertObject("A", std::move(node->attributes));
  }

  // Each node has a unique ID that also needs to be referenced
  // in a separate IDTree node, along with the lowest and highest
  // unique ID string.
  auto idString = PDFTagNode::nodeIdToString(node->nodeId);
  dict->insertByteString("ID", idString.c_str());
  IDTreeEntry idTreeEntry = {node->nodeId, ref};
  IDTreeEntries.push_back(idTreeEntry);

  return doc->emit(*dict, ref);
}

void PDFTagTree::addNodeAnnotation(int nodeId, PDFIndirectReference annotationRef,
                                   unsigned pageIndex) {
  if (!root) {
    return;
  }
  auto iter = nodeMap.find(nodeId);
  if (iter == nodeMap.end()) {
    return;
  }
  auto* tag = iter->second;
  DEBUG_ASSERT(tag);

  PDFTagNode::AnnotationInfo annotationInfo = {pageIndex, annotationRef};
  tag->annotations.push_back(annotationInfo);
}

void PDFTagTree::addNodeTitle(int nodeId, const std::vector<char>& title) {
  if (!root) {
    return;
  }
  auto iter = nodeMap.find(nodeId);
  if (iter == nodeMap.end()) {
    return;
  }
  auto* tag = iter->second;
  DEBUG_ASSERT(tag);

  if (tag->wantTitle) {
    tag->title.append(title.data(), title.size());
    // Arbitrary cutoff for size.
    if (tag->title.size() > 1023) {
      tag->wantTitle = false;
    }
  }
}

PDFIndirectReference PDFTagTree::makeStructTreeRoot(PDFDocument* doc) {
  if (!root || can_discard(root.get())) {
    return PDFIndirectReference();
  }

  PDFIndirectReference ref = doc->reserveRef();

  auto pageCount = static_cast<uint32_t>(doc->pageCount());

  // Build the StructTreeRoot.
  PDFDictionary structTreeRoot("StructTreeRoot");
  structTreeRoot.insertRef("K", PrepareTagTreeToEmit(ref, root.get(), doc));
  structTreeRoot.insertInt("ParentTreeNextKey", static_cast<int>(pageCount));

  // Build the parent tree, which consists of two things:
  // (1) For each page, a mapping from the marked content IDs on
  // each page to their corresponding tags
  // (2) For each annotation, an indirect reference to that
  // annotation's struct tree element.
  PDFDictionary parentTree("ParentTree");
  auto parentTreeNums = MakePDFArray();

  // First, one entry per page.
  DEBUG_ASSERT(static_cast<uint32_t>(marksPerPage.size()) <= pageCount);
  for (size_t j = 0; j < marksPerPage.size(); ++j) {
    auto pageMarks = marksPerPage[j];
    PDFArray markToTagArray;
    for (PDFTagNode* mark : pageMarks) {
      DEBUG_ASSERT(mark->ref);
      markToTagArray.appendRef(mark->ref);
    }
    parentTreeNums->appendInt(static_cast<int>(j));
    parentTreeNums->appendRef(doc->emit(markToTagArray));
  }

  // Then, one entry per annotation.
  for (size_t j = 0; j < parentTreeAnnotationNodeIds.size(); ++j) {
    int nodeId = parentTreeAnnotationNodeIds[j];
    int structParentKey = FirstAnnotationStructParentKey + static_cast<int>(j);

    auto iter = nodeMap.find(nodeId);
    if (iter == nodeMap.end()) {
      continue;
    }
    auto* tag = iter->second;
    parentTreeNums->appendInt(structParentKey);
    parentTreeNums->appendRef(tag->ref);
  }

  parentTree.insertObject("Nums", std::move(parentTreeNums));
  structTreeRoot.insertRef("ParentTree", doc->emit(parentTree));

  // Build the IDTree, a mapping from every unique ID string to
  // a reference to its corresponding structure element node.
  if (!IDTreeEntries.empty()) {
    std::sort(IDTreeEntries.begin(), IDTreeEntries.end(),
              [](const IDTreeEntry& a, const IDTreeEntry& b) { return a.nodeId < b.nodeId; });

    PDFDictionary idTree;
    PDFDictionary idTreeLeaf;
    auto limits = MakePDFArray();
    auto lowestNodeIdString = PDFTagNode::nodeIdToString(IDTreeEntries.begin()->nodeId);
    limits->appendByteString(lowestNodeIdString);
    auto highestNodeIdString = PDFTagNode::nodeIdToString(IDTreeEntries.rbegin()->nodeId);
    limits->appendByteString(highestNodeIdString);
    idTreeLeaf.insertObject("Limits", std::move(limits));
    auto names = MakePDFArray();
    for (const IDTreeEntry& entry : IDTreeEntries) {
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
  if (!node->title.empty()) {
    text = node->title;
  } else if (!node->alt.empty()) {
    text = node->alt;
  }

  // The uppermost/leftmost point on the earliest page of this node's marks.
  Location markPoint;
  for (auto&& mark : node->markedContent) {
    markPoint.accumulate(mark.location);
  }

  OutlineEntry::Content content{std::move(text), std::move(markPoint)};

  // Accumulate children
  const auto& children = node->children;
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
  char const* type = node->typeString.c_str();
  if (type[0] == 'H' && '1' <= type[1] && type[1] <= '6') {
    int level = type[1] - '0';
    while (level <= stack.back()->fHeaderLevel) {
      stack.pop_back();
    }
    OutlineEntry::Content content = create_outline_entry_content(node);
    if (!content.fText.empty()) {
      OutlineEntry e{std::move(content), level, doc->reserveRef(), node->ref};
      stack.push_back(&stack.back()->fChildren.emplace_back(std::move(e)));
      return;
    }
  }

  const auto& children = node->children;
  for (auto&& child : *children) {
    if (can_discard(&child)) {
      continue;
    }
    create_outline_from_headers(doc, &child, stack);
  }
}

}  // namespace

PDFIndirectReference PDFTagTree::makeOutline(PDFDocument* doc) {
  if (!root || can_discard(root.get()) ||
      outline != PDFMetadata::Outline::StructureElementHeaders) {
    return PDFIndirectReference();
  }

  std::vector<OutlineEntry*> stack;
  OutlineEntry top{{"", Location()}, 0, {}, {}};
  stack.push_back(&top);
  create_outline_from_headers(doc, root.get(), stack);
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

std::string PDFTagTree::getRootLanguage() {
  return root ? root->lang : "";
}

}  // namespace tgfx