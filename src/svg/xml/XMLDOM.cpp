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
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "../src/core/utils/Log.h"
#include "tgfx/core/Data.h"
#include "tgfx/svg/xml/XMLParser.h"

namespace tgfx {

DOM::DOM() = default;
DOM::~DOM() = default;

const std::shared_ptr<DOM::Node>& DOM::getRootNode() const {
  return _root;
}

std::shared_ptr<DOM::Node> DOM::getFirstChild(const std::shared_ptr<Node>& node,
                                              const std::string& name) {
  ASSERT(node);
  auto child = node->firstChild;
  if (!name.empty()) {
    for (; child != nullptr; child = child->nextSibling) {
      if (child->name != name) {
        break;
      }
    }
  }
  return child;
}

std::shared_ptr<DOM::Node> DOM::getNextSibling(const std::shared_ptr<Node>& node,
                                               const std::string& name) {
  ASSERT(node);
  auto sibling = node->nextSibling;
  if (!name.empty()) {
    for (; sibling != nullptr; sibling = sibling->nextSibling) {
      if (sibling->name != name) {
        break;
      }
    }
  }
  return sibling;
}

DOM::Type DOM::getType(const std::shared_ptr<Node>& node) {
  ASSERT(node);
  return static_cast<Type>(node->type);
}

std::string DOM::findAttribute(const std::shared_ptr<Node>& node, const std::string& name) {
  ASSERT(node);
  const auto& attrs = node->attrs();
  for (const Attr& attr : attrs) {
    if (attr.name == name) {
      return attr.value;
    }
  }
  return "";
}

#include "tgfx/svg/xml/XMLParser.h"

class DOMParser : public XMLParser {
 public:
  DOMParser() {
    _root = nullptr;
    _level = 0;
    _needToFlush = true;
  }

  std::shared_ptr<DOM::Node> getRoot() const {
    return _root;
  }

 protected:
  void flushAttributes() {
    ASSERT(_level > 0);

    auto node = std::make_shared<DOM::Node>();
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

  bool onStartElement(const char elem[]) override {
    this->startCommon(elem, strlen(elem), DOM::Element_Type);
    return false;
  }

  bool onAddAttribute(const char name[], const char value[]) override {
    _attributes.push_back({name, value});
    return false;
  }

  bool onEndElement(const char /*elem*/[]) override {
    if (_needToFlush) this->flushAttributes();
    _needToFlush = false;
    --_level;

    auto parent = _parentStack.back();
    _parentStack.pop_back();

    auto child = parent->firstChild;
    std::shared_ptr<DOM::Node> prev = nullptr;
    while (child) {
      auto next = child->nextSibling;
      child->nextSibling = prev;
      prev = child;
      child = next;
    }
    parent->firstChild = prev;
    return false;
  }

  bool onText(const char text[], int len) override {
    this->startCommon(text, static_cast<size_t>(len), DOM::Text_Type);
    this->DOMParser::onEndElement(_elementName.c_str());

    return false;
  }

 private:
  void startCommon(const char elem[], size_t elemSize, DOM::Type type) {
    if (_level > 0 && _needToFlush) {
      this->flushAttributes();
    }
    _needToFlush = true;
    _elementName = std::string(elem, elemSize);
    _elementType = type;
    ++_level;
  }

  std::vector<std::shared_ptr<DOM::Node>> _parentStack;
  std::shared_ptr<DOM::Node> _root;
  bool _needToFlush;

  // state needed for flushAttributes()
  std::vector<DOM::Attr> _attributes;
  std::string _elementName;
  DOM::Type _elementType;
  int _level;
};

std::shared_ptr<DOM::Node> DOM::build(const Data& data) {
  DOMParser parser;
  if (!parser.parse(data)) {
    _root = nullptr;
    return nullptr;
  }
  _root = parser.getRoot();
  return _root;
}

///////////////////////////////////////////////////////////////////////////

static void walk_dom(const DOM& dom, std::shared_ptr<DOM::Node> node, XMLParser* parser) {
  const auto* element = node->name.c_str();
  if (DOM::getType(node) == DOM::Text_Type) {
    ASSERT(dom.countChildren(node) == 0);
    parser->text(element, static_cast<int>(strlen(element)));
    return;
  }

  parser->startElement(element);

  for (const DOM::Attr& attr : node->attrs()) {
    parser->addAttribute(attr.name.c_str(), attr.value.c_str());
  }

  node = DOM::getFirstChild(node);
  while (node) {
    walk_dom(dom, node, parser);
    node = DOM::getNextSibling(node);
  }

  parser->endElement(element);
}

std::shared_ptr<DOM::Node> DOM::copy(const DOM& dom, const std::shared_ptr<DOM::Node>& node) {
  DOMParser parser;

  walk_dom(dom, node, &parser);

  _root = parser.getRoot();
  return _root;
}

XMLParser* DOM::beginParsing() {
  ASSERT(!_parser);
  _parser = std::make_unique<DOMParser>();

  return _parser.get();
}

std::shared_ptr<DOM::Node> DOM::finishParsing() {
  ASSERT(_parser);
  _root = _parser->getRoot();
  _parser.reset();

  return _root;
}

int DOM::countChildren(const std::shared_ptr<Node>& node, const std::string& name) {
  int count = 0;
  auto temp_node = getFirstChild(node, name);
  while (temp_node) {
    count += 1;
    temp_node = getNextSibling(temp_node, name);
  }
  return count;
}
}  // namespace tgfx