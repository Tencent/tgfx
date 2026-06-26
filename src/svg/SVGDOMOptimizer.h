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

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

using SVGIDMapper = std::unordered_map<std::string, std::shared_ptr<SVGNode>>;

/**
 * Post-construction optimizer for the SVG DOM tree. Detects patterns where tgfx exports a layer
 * with filter styles as two separate elements (content + filter carrier with identical shape),
 * and merges them into a single element with the filter applied directly.
 */
class SVGDOMOptimizer {
 public:
  static void OptimizeFilterPairs(SVGContainer* root, const SVGIDMapper& idMapper);

 private:
  static void processContainer(SVGContainer* container, const SVGIDMapper& idMapper);
  static bool isPureFilterGroup(const SVGNode* node);
  static bool isShapeElement(const SVGNode* node);
  static bool isMatchingShape(const SVGNode* a, const SVGNode* b);
  static bool isWhiteFill(const SVGNode* node);
  static void upgradeToInnerShadow(SVGContainer* filterNode);
  static bool isInnerShadowOnlyFilter(const SVGContainer* filterNode);

  struct FilterChain {
    std::vector<std::string> filterUrls;
    SVGNode* leafNode = nullptr;
  };
  static FilterChain unwrapFilterGroups(SVGNode* node);
};

}  // namespace tgfx
