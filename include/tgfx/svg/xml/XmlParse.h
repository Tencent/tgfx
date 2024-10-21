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

#include <string>

namespace tgfx {
class XmlParse {
 public:
  XmlParse();
  virtual ~XmlParse();

  /** Returns true for success*/
  bool parse(const std::string& xml);

 public:
  // public for ported implementation, not meant for clients to call
  bool startElement(const std::string& elem);
  bool addAttribute(const std::string& name, const std::string& value);
  bool endElement(const std::string& elem);
  bool text(const std::string& text);

 protected:
  // override in subclasses; return true to stop parsing
  virtual bool onStartElement(const std::string& elem);
  virtual bool onAddAttribute(const std::string& name, const std::string& value);
  virtual bool onEndElement(const std::string& elem);
  virtual bool onText(const std::string& text);
};
}  // namespace tgfx