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
#include <vector>
#include "tgfx/core/Data.h"

namespace tgfx {

class DOMParser;
class XMLParser;

struct DOMAttr {
  std::string name;
  std::string value;
};

struct DOMNode {
  std::string name;
  std::shared_ptr<DOMNode> firstChild;
  std::shared_ptr<DOMNode> nextSibling;
  std::vector<DOMAttr> attributes;
  uint8_t type;
  uint8_t pad;

  const std::vector<DOMAttr>& attrs() const {
    return attributes;
  }

  std::vector<DOMAttr>& attrs() {
    return attributes;
  }
};

class DOM {
 public:
  DOM();
  ~DOM();

  using Node = DOMNode;
  using Attr = DOMAttr;

  /** Returns null on failure
    */
  std::shared_ptr<Node> build(const Data& data);
  std::shared_ptr<Node> copy(const DOM& dom, const std::shared_ptr<Node>& node);

  const std::shared_ptr<Node>& getRootNode() const;

  XMLParser* beginParsing();
  std::shared_ptr<Node> finishParsing();

  enum Type { Element_Type, Text_Type };
  static Type getType(const std::shared_ptr<Node>& node);

  static std::shared_ptr<Node> getFirstChild(const std::shared_ptr<Node>& node,
                                             const std::string& name = "");

  static std::shared_ptr<Node> getNextSibling(const std::shared_ptr<Node>& node,
                                              const std::string& name = "");

  static std::string findAttribute(const std::shared_ptr<Node>& node, const std::string& attrName);

  // helpers for walking children
  static int countChildren(const std::shared_ptr<Node>& node, const std::string& name = "");

 private:
  std::shared_ptr<Node> _root = nullptr;
  std::unique_ptr<DOMParser> _parser;
};

}  // namespace tgfx
