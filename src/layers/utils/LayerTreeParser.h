/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "tgfx/layers/Layer.h"

namespace tgfx {

class VectorElement;
class ColorSource;
class ShapeStyle;

/**
 * Utility class to parse layer tree JSON files produced by LayerTreeDumper and reconstruct
 * the layer hierarchy.
 */
class LayerTreeParser {
 public:
  /**
   * Parses a layer tree JSON file and reconstructs the layer hierarchy.
   * @param jsonPath The file path of the JSON file to parse.
   * @return The root layer of the reconstructed tree, or nullptr on failure.
   */
  static std::shared_ptr<Layer> ParseTree(const std::string& jsonPath);
};

}  // namespace tgfx
