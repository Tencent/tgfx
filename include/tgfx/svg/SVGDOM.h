/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGFontManager.h"
#include "tgfx/svg/node/SVGSVG.h"

namespace tgfx {

class SVGNode;
using SVGIDMapper = std::unordered_map<std::string, std::shared_ptr<SVGNode>>;
class SVGDOM {
 public:
  static std::shared_ptr<SVGDOM> Make(const std::shared_ptr<Data>&);

  /**
   * Returns the root SVG node.
   */
  const std::shared_ptr<SVGSVG>& getRoot() const {
    return root;
  }

  void collectRenderFonts(const std::shared_ptr<SVGFontManager>&);
  /**
   * Renders the SVG to the provided canvas.
   */
  void render(Canvas*, const std::shared_ptr<SVGFontManager>&);

  /**
   * Specify a "container size" for the SVG dom.
   *
   * This is used to resolve the initial viewport when the root SVG width/height are specified
   * in relative units.
   *
   * If the root dimensions are in absolute units, then the container size has no effect since
   * the initial viewport is fixed.
   */
  void setContainerSize(const Size&);

  const Size& getContainerSize() const;

 private:
  SVGDOM(std::shared_ptr<SVGSVG>, SVGIDMapper&&);

  const std::shared_ptr<SVGSVG> root;
  const SVGIDMapper _nodeIDMapper;
  Size containerSize;
  std::shared_ptr<Picture> renderPicture;
};
}  // namespace tgfx