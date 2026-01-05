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

#include <deque>
#include <memory>
#include <vector>
#include "layers/DrawPolygon3D.h"

namespace tgfx {

/**
 * BspNode represents a node in the BSP tree.
 * Front/back are defined relative to the normal of the plane represented by 'data'.
 */
struct BspNode {
  explicit BspNode(std::unique_ptr<DrawPolygon3D> data);
  ~BspNode();

  std::unique_ptr<DrawPolygon3D> data = nullptr;
  std::vector<std::unique_ptr<DrawPolygon3D>> coplanarsFront = {};
  std::vector<std::unique_ptr<DrawPolygon3D>> coplanarsBack = {};
  std::unique_ptr<BspNode> frontChild = nullptr;
  std::unique_ptr<BspNode> backChild = nullptr;
};

/**
 * BspTree implements Binary Space Partitioning for correct depth sorting of 3D polygons.
 * It splits intersecting polygons along plane intersections.
 */
class BspTree {
 public:
  /**
   * Constructs a BSP tree from a list of polygons.
   * The first polygon is used as the root splitting plane.
   * @param polygons The list of polygons to process.
   */
  explicit BspTree(std::deque<std::unique_ptr<DrawPolygon3D>> polygons);

  ~BspTree();

  /**
   * Traverses the tree in back-to-front order relative to the camera.
   * Calls the action handler for each polygon in correct depth order.
   * @param action A callable that takes a const DrawPolygon3D* parameter.
   */
  template <typename Action>
  void traverseBackToFront(Action action) const {
    if (_root) {
      traverseNode(action, _root.get());
    }
  }

 private:
  void buildTree(BspNode* node, std::deque<std::unique_ptr<DrawPolygon3D>>* polygons);

  /**
   * Traverses polygons back-to-front along the screen Z-axis (positive Z points towards the
   * camera).
   */
  template <typename Action>
  void traverseNode(Action& action, const BspNode* node) const {
    if (node->data->isFacingPositiveZ()) {
      if (node->backChild) {
        traverseNode(action, node->backChild.get());
      }
      for (const auto& p : node->coplanarsBack) {
        action(p.get());
      }
      action(node->data.get());
      for (const auto& p : node->coplanarsFront) {
        action(p.get());
      }
      if (node->frontChild) {
        traverseNode(action, node->frontChild.get());
      }
    } else {
      if (node->frontChild) {
        traverseNode(action, node->frontChild.get());
      }
      for (const auto& p : node->coplanarsFront) {
        action(p.get());
      }
      action(node->data.get());
      for (const auto& p : node->coplanarsBack) {
        action(p.get());
      }
      if (node->backChild) {
        traverseNode(action, node->backChild.get());
      }
    }
  }

  std::unique_ptr<BspNode> _root = nullptr;
};

}  // namespace tgfx
