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

#include "tgfx/svg/xml/XMLDOM.h"
#include <utility>
#include "XMLParser.h"
#include "core/utils/Log.h"
#include "tgfx/core/Data.h"

namespace tgfx {
class DOMParser : public XMLParser {
 public:
  DOMParser() {
    _root = nullptr;
    _level = 0;
    _needToFlush = true;
  }

  std::shared_ptr<DOMNode> getRoot() const {
    return _root;
  }

 protected:
  void flushAttributes() {
    ASSERT(_level > 0);

    auto node = std::make_shared<DOMNode>();
    node->name = _elementName;
    node->firstChild = nullptr;
    node->attributes.swap(this->_attributes);
    node->type = _elementType;

    if (_root == nullptr) {
      node->nextSibling = nullptr;
      _root = node;
    } else {  // this adds siblings in reverse order. gets corrected in onEndElement()
      auto parent = _parentStack.back();
      ASSERT(_root && parent);
      node->nextSibling = parent->firstChild;
      parent->firstChild = node;
    }
    _parentStack.push_back(node);
    _attributes.clear();
  }

  bool onStartElement(const std::string& element) override {
    this->startCommon(element, DOMNodeType::Element);
    return false;
  }

  bool onAddAttribute(const std::string& name, const std::string& value) override {
    _attributes.push_back({name, value});
    return false;
  }

  bool onEndElement(const std::string& /*element*/) override {
    if (_needToFlush) {
      this->flushAttributes();
    }
    _needToFlush = false;
    --_level;

    auto parent = _parentStack.back();
    _parentStack.pop_back();

    auto child = parent->firstChild;
    std::shared_ptr<DOMNode> prev = nullptr;
    while (child) {
      auto next = child->nextSibling;
      child->nextSibling = prev;
      prev = child;
      child = next;
    }
    parent->firstChild = prev;
    return false;
  }

  bool onText(const std::string& text) override {
    this->startCommon(text, DOMNodeType::Text);
    this->DOMParser::onEndElement(_elementName.c_str());

    return false;
  }

 private:
  void startCommon(const std::string& element, DOMNodeType type) {
    if (_level > 0 && _needToFlush) {
      this->flushAttributes();
    }
    _needToFlush = true;
    _elementName = element;
    _elementType = type;
    ++_level;
  }

  std::vector<std::shared_ptr<DOMNode>> _parentStack;
  std::shared_ptr<DOMNode> _root;
  bool _needToFlush;

  // state needed for flushAttributes()
  std::vector<DOMAttribute> _attributes;
  std::string _elementName;
  DOMNodeType _elementType;
  int _level;
};

DOM::DOM(std::shared_ptr<DOMNode> root) : _root(std::move(root)) {};

DOM::~DOM() = default;

std::shared_ptr<DOM> DOM::MakeFromData(const Data& data) {
  DOMParser parser;
  if (!parser.parse(data)) {
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