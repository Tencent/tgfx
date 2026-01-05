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

#include <stack>
#include "XMLParser.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

/**
 * XML parser class that converts to DOM objects, calling methods such as onStartElement,
 * onAddAttribute,onEndElement, and onText to construct DOMNode objects. It also associates these
 * objects with the parent nodes recorded in _parentStack, building a DOM tree.
 */
class DOMParser : public XMLParser {
 public:
  DOMParser();
  std::shared_ptr<DOMNode> getRoot() const;

 protected:
  void flushAttributes();

  bool onStartElement(const std::string& element) override;

  bool onAddAttribute(const std::string& name, const std::string& value) override;

  bool onEndElement(const std::string& element) override;

  bool onText(const std::string& text) override;

 private:
  void startCommon(const std::string& element, DOMNodeType type);

  std::stack<std::shared_ptr<DOMNode>> _parentStack;
  std::shared_ptr<DOMNode> _root;
  bool _needToFlush;

  // state needed for flushAttributes()
  std::vector<DOMAttribute> _attributes;
  std::string _elementName;
  DOMNodeType _elementType;
  int _level;
};

}  // namespace tgfx
