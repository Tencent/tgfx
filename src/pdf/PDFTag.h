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
#include <unordered_map>
#include "pdf/PDFTypes.h"
#include "tgfx/core/Point.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

class PDFDocument;

struct Location {
  Point point{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
  uint32_t pageIndex{0};

  void accumulate(Location const& child) {
    if (std::isfinite(child.point.x) || std::isfinite(child.point.y)) {
      return;
    }
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      *this = child;
      return;
    }
    if (child.pageIndex < pageIndex) {
      *this = child;
      return;
    }
    if (child.pageIndex == pageIndex) {
      point.x = std::min(child.point.x, point.x);
      point.y = std::max(child.point.y, point.y);  // PDF y-up
      return;
    }
  }
};

struct PDFTagNode {
  // Structure element nodes need a unique alphanumeric ID,
  // and we need to be able to output them sorted in lexicographic
  // order. This helper function takes one of our node IDs and
  // builds an ID string that zero-pads the digits so that lexicographic
  // order matches numeric order.
  static std::string nodeIdToString(int nodeId) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "node%08d", nodeId);
    return std::string(buffer);
  }

  std::unique_ptr<std::vector<PDFTagNode>> fChildren = nullptr;
  struct MarkedContentInfo {
    Location fLocation;
    int fMarkId;
  };
  std::vector<MarkedContentInfo> fMarkedContent;
  int fNodeId;
  bool fWantTitle;
  std::string fTypeString;
  std::string fTitle;
  std::string fAlt;
  std::string fLang;
  PDFIndirectReference fRef;
  enum class State {
    kUnknown,
    kYes,
    kNo,
  };
  State fCanDiscard = State::kUnknown;
  std::unique_ptr<PDFArray> fAttributes;
  struct AnnotationInfo {
    unsigned fPageIndex;
    PDFIndirectReference fAnnotationRef;
  };
  std::vector<AnnotationInfo> fAnnotations;
};

class SkPDFTagTree {
 public:
  SkPDFTagTree();
  SkPDFTagTree(const SkPDFTagTree&) = delete;
  SkPDFTagTree& operator=(const SkPDFTagTree&) = delete;
  ~SkPDFTagTree();

  void init(PDFStructureElementNode*, PDFMetadata::Outline);

  class Mark {
    PDFTagNode* const fNode;
    size_t const fMarkIndex;

   public:
    Mark(PDFTagNode* node, size_t index) : fNode(node), fMarkIndex(index) {
    }
    Mark() : Mark(nullptr, 0) {
    }
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
    Mark(Mark&&) = default;
    Mark& operator=(Mark&&) = delete;

    explicit operator bool() const {
      return fNode;
    }
    int id();
    Point& point();
  };

  // Used to allow marked content to refer to its corresponding structure
  // tree node, via a page entry in the parent tree. Returns a false mark if
  // nodeId is 0.
  Mark createMarkIdForNodeId(int nodeId, unsigned pageIndex, Point);
  // Used to allow annotations to refer to their corresponding structure
  // tree node, via the struct parent tree. Returns -1 if no struct parent
  // key.
  int createStructParentKeyForNodeId(int nodeId, unsigned pageIndex);

  void addNodeAnnotation(int nodeId, PDFIndirectReference annotationRef, unsigned pageIndex);
  void addNodeTitle(int nodeId, const std::vector<char>& title);
  PDFIndirectReference makeStructTreeRoot(PDFDocument* doc);
  PDFIndirectReference makeOutline(PDFDocument* doc);
  std::string getRootLanguage();

 private:
  // An entry in a map from a node ID to an indirect reference to its
  // corresponding structure element node.
  struct IDTreeEntry {
    int nodeId;
    PDFIndirectReference ref;
  };

  void Copy(PDFStructureElementNode& node, PDFTagNode* dst,
            std::unordered_map<int, PDFTagNode*>* nodeMap, bool wantTitle);
  PDFIndirectReference PrepareTagTreeToEmit(PDFIndirectReference parent, PDFTagNode* node,
                                            PDFDocument* doc);

  std::unordered_map<int, PDFTagNode*> fNodeMap;
  std::unique_ptr<PDFTagNode> fRoot = nullptr;
  PDFMetadata::Outline fOutline;
  std::vector<std::vector<PDFTagNode*>> fMarksPerPage;
  std::vector<IDTreeEntry> fIdTreeEntries;
  std::vector<int> fParentTreeAnnotationNodeIds;
};

}  // namespace tgfx