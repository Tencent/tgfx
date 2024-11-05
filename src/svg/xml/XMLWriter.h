/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <cstdint>
#include <memory>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include "tgfx/svg/xml/XMLDOM.h"

class XMLParser;

namespace tgfx {

/**
 * Abstract class for writing XML, providing two implementations:
 * 1. Use the writeDOM interface to input a DOM tree and serialize it into XML.
 * 2. Use the startElement, endElement, addAttribute, and addText interfaces to manually construct
 *    XML.
 */
class XMLWriter {
 public:
  explicit XMLWriter(bool doEscapeFlag = true);
  virtual ~XMLWriter();

  void addAttribute(const std::string& name, const std::string& value);
  void addS32Attribute(const std::string& name, int32_t value);
  void addHexAttribute(const std::string& name, uint32_t value, uint32_t minDigits = 0);
  void addScalarAttribute(const std::string& name, float value);
  void addText(const std::string& text);

  void startElement(const std::string& element);
  void endElement() {
    this->onEndElement();
  }
  void writeDOM(const std::shared_ptr<DOM>& DOM, bool skipRoot);
  virtual void writeHeader();

 protected:
  virtual void onStartElement(const std::string& element) = 0;
  virtual void onAddAttribute(const std::string& name, const std::string& value) = 0;
  virtual void onAddText(const std::string& text) = 0;
  virtual void onEndElement() = 0;

  struct Elem {
    explicit Elem(std::string elementName) : name(std::move(elementName)) {
    }
    std::string name;
    bool hasChildren = false;
    bool hasText = false;
  };

  // Maintain a stack to record the current element being processed and its parent elements
  void flush();
  void doEnd();
  bool doStart(const std::string& elementName);
  const Elem& getEnd();
  std::stack<Elem> _elementsStack;

  static std::string_view getHeader();

 private:
  bool _doEscapeFlag = true;
  // illegal operator
  XMLWriter& operator=(const XMLWriter&) = delete;
};

/**
 * XMLStreamWriter is an XMLWriter implementation that writes XML to a string stream.
 */
class XMLStreamWriter : public XMLWriter {
 public:
  enum : uint32_t {
    NoPretty_Flag = 0x01,
  };

  explicit XMLStreamWriter(std::stringstream& stream, uint32_t flags = 0);
  ~XMLStreamWriter() override;
  void writeHeader() override;

 protected:
  void onStartElement(const std::string& element) override;
  void onEndElement() override;
  void onAddAttribute(const std::string& name, const std::string& value) override;
  void onAddText(const std::string& text) override;

 private:
  void newline();
  void tab(int level);

  std::stringstream& _stream;
  const uint32_t _flags;
};

/**
 * XMLParserWriter is an XMLWriter implementation that writes XML to an XMLParser.
 * Calls XMLParser's startElement, addAttribute, endElement, and text interfaces for each element.
 */
class XMLParserWriter : public XMLWriter {
 public:
  explicit XMLParserWriter(XMLParser*);
  ~XMLParserWriter() override;

 protected:
  void onStartElement(const std::string& element) override;
  void onEndElement() override;
  void onAddAttribute(const std::string& name, const std::string& value) override;
  void onAddText(const std::string& text) override;

 private:
  XMLParser& _parser;
};

}  // namespace tgfx
