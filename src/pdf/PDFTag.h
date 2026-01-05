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

#include <memory>
#include <unordered_map>
#include "pdf/PDFTypes.h"
#include "tgfx/core/Point.h"
#include "tgfx/pdf/PDFMetadata.h"

namespace tgfx {

class PDFDocumentImpl;

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
  // Structure element nodes need a unique alphanumeric ID, and we need to be able to output them
  // sorted in lexicographic order. This helper function takes one of our node IDs and builds an ID
  // string that zero-pads the digits so that lexicographic order matches numeric order.
  static std::string nodeIdToString(int nodeId) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "node%08d", nodeId);
    return std::string(buffer);
  }

  std::unique_ptr<std::vector<PDFTagNode>> children = nullptr;
  struct MarkedContentInfo {
    Location location;
    int markId;
  };

  std::vector<MarkedContentInfo> markedContent;
  int nodeId;
  bool wantTitle;
  std::string typeString;
  std::string title;
  std::string alt;
  std::string lang;
  PDFIndirectReference ref;
  enum class State {
    Unknown,
    Yes,
    No,
  };
  State canDiscard = State::Unknown;
  std::unique_ptr<PDFArray> attributes;
  struct AnnotationInfo {
    unsigned pageIndex;
    PDFIndirectReference annotationRef;
  };
  std::vector<AnnotationInfo> annotations;
};

class PDFTagTree {
 public:
  PDFTagTree();
  PDFTagTree(const PDFTagTree&) = delete;
  PDFTagTree& operator=(const PDFTagTree&) = delete;
  ~PDFTagTree();

  void init(PDFStructureElementNode*, PDFMetadata::Outline);

  class Mark {
   public:
    Mark(PDFTagNode* node, size_t index) : node(node), markIndex(index) {
    }
    Mark() : Mark(nullptr, 0) {
    }
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
    Mark(Mark&&) = default;
    Mark& operator=(Mark&&) = delete;

    explicit operator bool() const {
      return node;
    }
    int id();
    Point& point();

   private:
    PDFTagNode* const node;
    const size_t markIndex;
  };

  Mark createMarkIdForNodeId(int nodeId, unsigned pageIndex, Point);

  int createStructParentKeyForNodeId(int nodeId, unsigned pageIndex);

  void addNodeAnnotation(int nodeId, PDFIndirectReference annotationRef, unsigned pageIndex);

  void addNodeTitle(int nodeId, const std::vector<char>& title);

  PDFIndirectReference makeStructTreeRoot(PDFDocumentImpl* document);

  PDFIndirectReference makeOutline(PDFDocumentImpl* document);

  std::string getRootLanguage();

 private:
  struct IDTreeEntry {
    int nodeId;
    PDFIndirectReference ref;
  };

  void Copy(PDFStructureElementNode& node, PDFTagNode* destination,
            std::unordered_map<int, PDFTagNode*>* nodeMap, bool wantTitle);

  PDFIndirectReference PrepareTagTreeToEmit(PDFIndirectReference parent, PDFTagNode* node,
                                            PDFDocumentImpl* document);

  std::unordered_map<int, PDFTagNode*> nodeMap;
  std::unique_ptr<PDFTagNode> root = nullptr;
  PDFMetadata::Outline outline;
  std::vector<std::vector<PDFTagNode*>> marksPerPage;
  std::vector<IDTreeEntry> IDTreeEntries;
  std::vector<int> parentTreeAnnotationNodeIds;
};

}  // namespace tgfx
