
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

#include <string>
#include "tgfx/core/Data.h"
#include "tgfx/core/Stream.h"

namespace tgfx {
class XMLParser {
 public:
  XMLParser();
  virtual ~XMLParser();

  /**
   * Parses stream in XML format, with the parsing results returned via callback functions.
   * @param stream The stream to be parsed.
   * @return true if parsing is successful.
   * @return false if parsing fails.
   */
  bool parse(Stream& stream);

 protected:
  /**
   * Override in subclasses; return true to stop parsing
   * Each function represents a parsing stage of an XML element.
   */
  virtual bool onStartElement(const std::string& element) = 0;
  virtual bool onAddAttribute(const std::string& name, const std::string& value) = 0;
  virtual bool onEndElement(const std::string& element) = 0;
  virtual bool onText(const std::string& text) = 0;

 public:
  /**
   * public for internal parser library calls, not intended for client call
   */
  bool startElement(const std::string& element);
  bool addAttribute(const std::string& name, const std::string& value);
  bool endElement(const std::string& element);
  bool text(const std::string& text);
};
}  // namespace tgfx
