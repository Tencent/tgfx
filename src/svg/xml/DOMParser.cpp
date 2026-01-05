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

#include "DOMParser.h"
#include "core/utils/Log.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

DOMParser::DOMParser() {
  _root = nullptr;
  _level = 0;
  _needToFlush = true;
}

std::shared_ptr<DOMNode> DOMParser::getRoot() const {
  return _root;
}

void DOMParser::flushAttributes() {
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
    auto parent = _parentStack.top();
    ASSERT(_root && parent);
    node->nextSibling = parent->firstChild;
    parent->firstChild = node;
  }
  _parentStack.push(node);
  _attributes.clear();
}

bool DOMParser::onStartElement(const std::string& element) {
  this->startCommon(element, DOMNodeType::Element);
  return false;
}

bool DOMParser::onAddAttribute(const std::string& name, const std::string& value) {
  _attributes.push_back({name, value});
  return false;
}

bool DOMParser::onEndElement(const std::string& /*element*/) {
  if (_needToFlush) {
    this->flushAttributes();
  }
  _needToFlush = false;
  --_level;

  auto parent = _parentStack.top();
  _parentStack.pop();

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

bool DOMParser::onText(const std::string& text) {
  // Ignore text if it is empty or contains only whitespace and newlines
  if (text.find_first_not_of(" \n") != std::string::npos) {
    this->startCommon(text, DOMNodeType::Text);
    this->DOMParser::onEndElement(_elementName);
  }
  return false;
}

void DOMParser::startCommon(const std::string& element, DOMNodeType type) {
  if (_level > 0 && _needToFlush) {
    this->flushAttributes();
  }
  _needToFlush = true;
  _elementName = element;
  _elementType = type;
  ++_level;
}
}  // namespace tgfx
