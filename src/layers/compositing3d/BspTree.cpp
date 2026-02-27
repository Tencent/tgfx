/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "BspTree.h"

namespace tgfx {

BspNode::BspNode(std::unique_ptr<DrawPolygon3D> polygon) : data(std::move(polygon)) {
}

BspNode::~BspNode() = default;

BspTree::BspTree(std::deque<std::unique_ptr<DrawPolygon3D>> polygons) {
  if (polygons.empty()) {
    return;
  }

  _root = std::make_unique<BspNode>(std::move(polygons.front()));
  polygons.pop_front();
  buildTree(_root.get(), &polygons);
}

BspTree::~BspTree() = default;

/**
 * Recursively partitions polygons by the node's plane into front/back lists, then builds subtrees.
 * Complexity: O(n log n) average case, O(n * 2^n) worst case when every split intersects all
 * remaining polygons.
 */
void BspTree::buildTree(BspNode* node, std::deque<std::unique_ptr<DrawPolygon3D>>* polygons) {
  std::deque<std::unique_ptr<DrawPolygon3D>> frontList;
  std::deque<std::unique_ptr<DrawPolygon3D>> backList;

  while (!polygons->empty()) {
    auto polygon = std::move(polygons->front());
    polygons->pop_front();

    std::unique_ptr<DrawPolygon3D> newFront;
    std::unique_ptr<DrawPolygon3D> newBack;
    bool isCoplanar = false;

    node->data->splitAnother(std::move(polygon), &newFront, &newBack, &isCoplanar);

    if (isCoplanar) {
      if (newFront) {
        node->coplanarsFront.push_back(std::move(newFront));
      }
      if (newBack) {
        node->coplanarsBack.push_back(std::move(newBack));
      }
    } else {
      if (newFront) {
        frontList.push_back(std::move(newFront));
      }
      if (newBack) {
        backList.push_back(std::move(newBack));
      }
    }
  }

  if (!backList.empty()) {
    node->backChild = std::make_unique<BspNode>(std::move(backList.front()));
    backList.pop_front();
    buildTree(node->backChild.get(), &backList);
  }

  if (!frontList.empty()) {
    node->frontChild = std::make_unique<BspNode>(std::move(frontList.front()));
    frontList.pop_front();
    buildTree(node->frontChild.get(), &frontList);
  }
}

}  // namespace tgfx
