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

#include <sys/_types/_u_int32_t.h>
#include <sys/_types/_u_int8_t.h>
#include <memory>
#include <tuple>
#include <vector>
#include "tgfx/core/Data.h"

namespace tgfx {

class DOMParser;
class XMLParser;

struct DOMAttr {
  std::string name;
  std::string value;
};

enum class DOMNodeType : u_int8_t { Element, Text };

struct DOMNode {
  std::string name;
  std::shared_ptr<DOMNode> firstChild;
  std::shared_ptr<DOMNode> nextSibling;
  std::vector<DOMAttr> attributes;
  DOMNodeType type;

  /**
   * @brief Get the first child object, optionally filtered by name.
   * 
   * @param name 
   * @return DOMNode object.
   */
  std::shared_ptr<DOMNode> getFirstChild(const std::string& name = "");

  /**
   * @brief Get the next Sibling object, optionally filtered by name.
   * 
   * @param name 
   * @return DOMNode object.
   */
  std::shared_ptr<DOMNode> getNextSibling(const std::string& name = "");

  /**
   * @brief Get the value content of the node by attribute name.
   * 
   * @return value content.
   */
  std::tuple<bool, std::string> findAttribute(const std::string& attrName);

  /**
   * @brief Count the number of children of the node,optionally filtered by name.
   * 
   * @param name 
   * @return The number of children.
   */
  int countChildren(const std::string& name = "");
};

class DOM {
 public:
  /**
   * @brief Destructor.
   */
  ~DOM();

  /**
   * @brief Constructs a DOM tree from XML text data.
   * 
   * @param data XML text data.
   * @return The DOM tree. Returns nullptr if construction fails.
   */
  static std::shared_ptr<DOM> MakeFromData(const Data& data);

  /**
   * @brief Creates a deep copy of a DOM tree.
   * 
   * @param inputDOM The DOM tree to copy.
   * @return The copied DOM tree. Returns nullptr if copying fails.
   */
  static std::shared_ptr<DOM> copy(const std::shared_ptr<DOM>& inputDOM);

  /**
   * @brief Gets the root node of the DOM tree.
   * 
   * @return The root node.
   */
  std::shared_ptr<DOMNode> getRootNode() const;

 private:
  /**
   * @brief Constructor.
   * 
   * @param root The root node of the DOM tree.
   */
  DOM(std::shared_ptr<DOMNode> root);

  std::shared_ptr<DOMNode> _root = nullptr;
};

}  // namespace tgfx
