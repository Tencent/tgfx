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
#include "tgfx/svg/xml/XmlParse.h"
namespace tgfx {

struct DomAttr {
  std::string _name;
  std::string _value;
};

struct DomNode {
  std::string _name;
  std::vector<std::shared_ptr<DomNode>> _children;
  std::shared_ptr<DomAttr> _attrs;

  uint16_t _attrCount;
  uint8_t _type;
  uint8_t _pad;

  const std::shared_ptr<DomAttr> attrs() const {
    return _attrs;
  }

  std::shared_ptr<DomAttr> attrs() {
    return _attrs;
  }
};

class XmlDom : public XmlParse {
 public:
  XmlDom();
  virtual ~XmlDom();

 protected:
  // override in subclasses; return true to stop parsing
  bool onStartElement(const std::string& elem) override;
  bool onAddAttribute(const std::string& name, const std::string& value) override;
  bool onEndElement(const std::string& elem) override;
  bool onText(const std::string& text) override;

 private:
  std::shared_ptr<DomNode> _root;
};
}  // namespace tgfx