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

#include "tgfx/svg/xml/XMLDOM.h"
#include <utility>
#include "DOMParser.h"
#include "XMLParser.h"
#include "../../../include/tgfx/core/Log.h"
#include "tgfx/core/Data.h"

namespace tgfx {

DOM::DOM(std::shared_ptr<DOMNode> root) {
  _root = std::move(root);
};

DOM::~DOM() = default;

std::shared_ptr<DOM> DOM::Make(Stream& stream) {
  DOMParser parser;
  if (!parser.parse(stream)) {
    return nullptr;
  }
  auto root = parser.getRoot();
  auto dom = std::shared_ptr<DOM>(new DOM(root));
  return dom;
}

static void walk_dom(std::shared_ptr<DOMNode> node, XMLParser* parser) {
  auto element = node->name;
  if (node->type == DOMNodeType::Text) {
    ASSERT(node->countChildren() == 0);
    parser->text(element);
    return;
  }

  parser->startElement(element);

  for (const DOMAttribute& attr : node->attributes) {
    parser->addAttribute(attr.name, attr.value);
  }

  node = node->getFirstChild();
  while (node) {
    walk_dom(node, parser);
    node = node->getNextSibling();
  }

  parser->endElement(element);
}

std::shared_ptr<DOM> DOM::copy(const std::shared_ptr<DOM>& inputDOM) {
  ASSERT(inputDOM);
  DOMParser parser;
  walk_dom(inputDOM->getRootNode(), &parser);
  auto copiedRoot = parser.getRoot();
  if (copiedRoot) {
    auto copiedDOM = std::shared_ptr<DOM>(new DOM(copiedRoot));
    return copiedDOM;
  }
  return nullptr;
}

std::shared_ptr<DOMNode> DOM::getRootNode() const {
  return _root;
}

std::shared_ptr<DOMNode> DOMNode::getFirstChild(const std::string& name) const {
  auto child = this->firstChild;
  if (!name.empty()) {
    for (; child != nullptr; child = child->nextSibling) {
      if (child->name != name) {
        break;
      }
    }
  }
  return child;
}

std::shared_ptr<DOMNode> DOMNode::getNextSibling(const std::string& name) const {
  auto sibling = this->nextSibling;
  if (!name.empty()) {
    for (; sibling != nullptr; sibling = sibling->nextSibling) {
      if (sibling->name != name) {
        break;
      }
    }
  }
  return sibling;
}

std::tuple<bool, std::string> DOMNode::findAttribute(const std::string& name) const {
  if (!name.empty()) {
    for (const DOMAttribute& attr : this->attributes) {
      if (attr.name == name) {
        return {true, attr.value};
      }
    }
  }
  return {false, ""};
}

int DOMNode::countChildren(const std::string& name) const {
  int count = 0;
  auto tempNode = this->getFirstChild(name);
  while (tempNode) {
    count += 1;
    tempNode = tempNode->getNextSibling(name);
  }
  return count;
}
}  // namespace tgfx