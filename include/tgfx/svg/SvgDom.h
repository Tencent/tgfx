/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/svg/node/SvgNode.h"

namespace tgfx {

/**
 * SvgDom represents the root of an SVG document.
 * it used to import the SVG file and parse it to SvgNode.
 */
class SvgDom {
 public:
  /**
    * Creates a new SvgDom instance from the specified string.parse the string to SvgNode and
    * construct the tree structure of the SvgNode.
    * parse steps:
    * 1. read the string by XmlParse.read each element and attribute will call functions 
    *   eg:startElement(),addAttribute(),endElement(),text().
    * 2. These functions are overridden in the XmlDom class and and construct the DomNode
    *   object in it.
    * 3. The DomNode object will be converted to SvgNode object.
    */
  static std::shared_ptr<SvgDom> MakeFromString(const std::string& str);

  /**
   * Returns the root node of the SVG document.
   */
  std::shared_ptr<SvgNode> getRoot();

 private:
  std::shared_ptr<SvgNode> _root = nullptr;
};
}  // namespace tgfx
