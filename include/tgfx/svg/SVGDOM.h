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

/**
 * 
 */
class SVGDOM {
 public:
  static std::shared_ptr<SVGDOM> Make(const std::shared_ptr<Data>&);

  /**
   * Returns the root SVG node.
   */
  const std::shared_ptr<SVGSVG>& getRoot() const;

  void collectRenderFonts(const std::shared_ptr<SVGFontManager>&);

  /**
   * Renders the SVG to the provided canvas.
   * @param canvas The canvas to render to.
   * @param fontManager The font manager for rendering SVG text. If no text rendering is needed, 
   * this can be nullptr, and text will not be rendered.
   */
  void render(Canvas* canvas, const std::shared_ptr<SVGFontManager>& fontManager = nullptr);

  /**
   * Sets the size of the container that the SVG will be rendered into.
   */
  void setContainerSize(const Size&);

  const Size& getContainerSize() const;

 private:
  SVGDOM(std::shared_ptr<SVGSVG>, SVGIDMapper&&);
  const std::shared_ptr<SVGSVG> root = nullptr;
  const SVGIDMapper nodeIDMapper = {};
  Size containerSize = Size::MakeEmpty();
};
}  // namespace tgfx