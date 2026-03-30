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

#include <fstream>
#include <sstream>
#include <string>
#include "tgfx/layers/Layer.h"

namespace tgfx {

class ShapeStyle;
class Shader;
class ColorSource;
class VectorElement;

/**
 * Utility class to dump the entire layer tree structure to JSON files for debugging and
 * reconstruction purposes.
 */
class LayerTreeDumper {
 public:
  /**
   * Dumps the layer tree starting from the given root layer to a JSON file.
   * The caller should pass the root layer (e.g., displayList->root()) directly.
   * The dumped data can be used to reconstruct the layer tree by creating a new DisplayList
   * and adding layers to its root.
   * @param root The root layer of the tree to dump.
   * @param outputDir The directory path where the output file will be saved.
   *                  The file will be named "layer_tree.json".
   */
  static void DumpTree(const Layer* root, const std::string& outputDir);

 private:
  static std::string LayerTypeToString(LayerType type);
  static std::string BlendModeToString(BlendMode mode);
  static std::string MaskTypeToString(LayerMaskType type);
  static std::string MatrixToJson(const Matrix& matrix);
  static std::string Matrix3DToJson(const Matrix3D& matrix);
  static std::string RectToJson(const Rect& rect);
  static std::string PointToJson(const Point& point);
  static std::string ColorToJson(const Color& color);
  static std::string EscapeString(const std::string& str);
  static void DumpLayerToStream(const Layer* layer, std::ostream& os, int indent,
                                bool isLast = true);
  static std::string GetIndent(int level);

  // Layer subclass serialization
  static void DumpSolidLayerProps(const Layer* layer, std::ostream& os, const std::string& ind);
  static void DumpShapeLayerProps(const Layer* layer, std::ostream& os, const std::string& ind);
  static void DumpTextLayerProps(const Layer* layer, std::ostream& os, const std::string& ind);
  static void DumpImageLayerProps(const Layer* layer, std::ostream& os, const std::string& ind);
  static void DumpVectorLayerProps(const Layer* layer, std::ostream& os, const std::string& ind,
                                   int indent);

  // LayerFilter serialization
  static void DumpFilters(const std::vector<std::shared_ptr<LayerFilter>>& filters,
                          std::ostream& os, const std::string& ind, int indent);

  // LayerStyle serialization
  static void DumpLayerStyles(const std::vector<std::shared_ptr<LayerStyle>>& styles,
                              std::ostream& os, const std::string& ind, int indent);

  // ShapeStyle serialization (for ShapeLayer)
  static void DumpShapeStyles(const std::vector<std::shared_ptr<ShapeStyle>>& styles,
                              std::ostream& os, const std::string& ind, int indent,
                              const char* fieldName);

  // Shader serialization (for ShapeStyle)
  static void DumpShader(const std::shared_ptr<Shader>& shader, std::ostream& os,
                         const std::string& ind, int indent);

  // VectorElement serialization
  static void DumpVectorElements(const std::vector<std::shared_ptr<VectorElement>>& elements,
                                 std::ostream& os, int indent);
  static void DumpVectorElement(const VectorElement* element, std::ostream& os, int indent,
                                bool isLast);

  // ColorSource serialization (for VectorElement FillStyle/StrokeStyle)
  static void DumpColorSource(const std::shared_ptr<ColorSource>& source, std::ostream& os,
                              int indent);
};

}  // namespace tgfx
