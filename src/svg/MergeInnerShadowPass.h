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

namespace tgfx {

class SVGContainer;
class SVGNode;

using SVGIDMapper = std::unordered_map<std::string, std::shared_ptr<SVGNode>>;

/**
 * Detects patterns where tgfx exports a layer with filter styles as two separate elements
 * (content + filter carrier with identical shape), and merges them into a single element with the
 * filter applied directly.
 */
void MergeInnerShadowFilters(SVGContainer* root, const SVGIDMapper& idMapper);

}  // namespace tgfx
