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

#include "tgfx/svg/xml/XMLWriter.h"
#include <cstdint>
#include <memory>
#include <string>
#include "tgfx/svg/xml/XMLDOM.h"
#include "tgfx/svg/xml/XMLParser.h"
// #include "include/core/SkStream.h"
// #include "include/private/base/SkTo.h"

namespace tgfx {

XMLWriter::XMLWriter(bool doEscapeMarkup) : fDoEscapeMarkup(doEscapeMarkup) {
}

XMLWriter::~XMLWriter() {
  // ASSERT(fElems.empty());
}

void XMLWriter::flush() {
  while (!fElems.empty()) {
    this->endElement();
  }
}

void XMLWriter::addS32Attribute(const std::string& name, int32_t value) {
  this->addAttribute(name, std::to_string(value));
}

void XMLWriter::addHexAttribute(const std::string& name, uint32_t value, uint32_t minDigits) {
  std::string temp("0x");
  std::stringstream stream;
  stream << std::hex << value;
  std::string result = stream.str();
  if (result.length() < minDigits) {
    result.insert(0, minDigits - result.length(), '0');
  }
  temp += result;
  this->addAttribute(name, temp);
}

void XMLWriter::addScalarAttribute(const std::string& name, float value) {
  this->addAttribute(name, std::to_string(value));
}

void XMLWriter::addText(const std::string& text) {
  if (fElems.empty()) {
    return;
  }
  this->onAddText(text);
  fElems.top().hasText = true;
}

void XMLWriter::doEnd() {
  fElems.pop();
}

bool XMLWriter::doStart(const std::string& elementName) {
  auto level = fElems.size();
  bool firstChild = level > 0 && !fElems.top().hasChildren;
  if (firstChild) {
    fElems.top().hasChildren = true;
  }
  fElems.emplace(elementName);
  return firstChild;
}

const XMLWriter::Elem& XMLWriter::getEnd() {
  return fElems.top();
}

std::string_view XMLWriter::getHeader() {
  return R"(<?xml version="1.0" encoding="utf-8" ?>)";
}

static const char* escape_char(char c, char storage[2]) {
  static const char* gEscapeChars[] = {"<&lt;", ">&gt;",
                                       //"\"&quot;",
                                       //"'&apos;",
                                       "&&amp;"};

  const char** array = gEscapeChars;
  for (unsigned i = 0; i < std::size(gEscapeChars); i++) {
    if (array[i][0] == c) {
      return &array[i][1];
    }
  }
  storage[0] = c;
  storage[1] = 0;
  return storage;
}

static size_t escape_markup(char dst[], const char src[], size_t length) {
  size_t extra = 0;
  const char* stop = src + length;

  while (src < stop) {
    char orig[2];
    const char* seq = escape_char(*src, orig);
    size_t seqSize = strlen(seq);

    if (dst) {
      strcpy(dst, seq);
      dst += seqSize;
    }

    // now record the extra size needed
    extra += seqSize - 1;  // minus one to subtract the original char

    // bump to the next src char
    src += 1;
  }
  return extra;
}

void XMLWriter::addAttribute(const std::string& name, const std::string& value) {
  if (fDoEscapeMarkup) {
    auto convertedValue = value;
    auto extra = escape_markup(nullptr, value.data(), value.size());
    if (extra) {
      convertedValue.resize(value.size() + extra);
      escape_markup(convertedValue.data(), value.data(), value.size());
    }
    this->onAddAttribute(name, value);
  } else {
    this->onAddAttribute(name, value);
  }
}

void XMLWriter::startElement(const std::string& element) {
  this->onStartElement(element);
}

////////////////////////////////////////////////////////////////////////////////////////

static void write_dom(const DOM& dom, std::shared_ptr<DOM::Node> node, XMLWriter* writer,
                      bool skipRoot) {
  if (!skipRoot) {
    std::string element = node->name;
    if (node->type == DOM::Text_Type) {
      // SkASSERT(dom.countChildren(node) == 0);
      writer->addText(element);
      return;
    }

    writer->startElement(element);

    for (const DOM::Attr& attr : node->attrs()) {
      writer->addAttribute(attr.name, attr.value);
    }
  }

  node = DOM::getFirstChild(node);
  while (node) {
    write_dom(dom, node, writer, false);
    node = DOM::getNextSibling(node);
  }

  if (!skipRoot) {
    writer->endElement();
  }
}

void XMLWriter::writeDOM(const DOM& dom, const std::shared_ptr<DOM::Node>& node, bool skipRoot) {
  if (node) {
    write_dom(dom, node, this, skipRoot);
  }
}

void XMLWriter::writeHeader() {
}

// SkXMLStreamWriter

XMLStreamWriter::XMLStreamWriter(std::stringstream& stream, uint32_t flags)
    : fStream(stream), fFlags(flags) {
}

XMLStreamWriter::~XMLStreamWriter() {
  this->flush();
}

void XMLStreamWriter::onAddAttribute(const std::string& name, const std::string& value) {
  // SkASSERT(!fElems.back()->fHasChildren && !fElems.back()->fHasText);
  fStream << " " << name << "=\"" << value << "\"" << value << "\"";
}

void XMLStreamWriter::onAddText(const std::string& text) {
  auto elem = fElems.top();

  if (!elem.hasChildren && !elem.hasText) {
    fStream << ">";
    this->newline();
  }

  this->tab(static_cast<int>(fElems.size()) + 1);
  fStream << text;
  this->newline();
}

void XMLStreamWriter::onEndElement() {
  auto element = getEnd();
  if (element.hasChildren || element.hasText) {
    this->tab(static_cast<int>(fElems.size()));
    fStream << "</" << element.name << ">";
  } else {
    fStream << "/>";
  }
  this->newline();
  doEnd();
}

void XMLStreamWriter::onStartElement(const std::string& element) {
  auto level = fElems.size();
  if (this->doStart(element)) {
    // the first child, need to close with >
    fStream << ">";
    this->newline();
  }

  this->tab(static_cast<int>(level));
  fStream << "<";
  fStream << element;
}

void XMLStreamWriter::writeHeader() {
  auto header = getHeader();
  fStream << header;
  this->newline();
}

void XMLStreamWriter::newline() {
  if (!(fFlags & kNoPretty_Flag)) {
    fStream << std::endl;
  }
}

void XMLStreamWriter::tab(int level) {
  if (!(fFlags & kNoPretty_Flag)) {
    for (int i = 0; i < level; i++) {
      fStream << "\t";
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
XMLParserWriter::XMLParserWriter(XMLParser* parser) : XMLWriter(false), fParser(*parser) {
}

XMLParserWriter::~XMLParserWriter() {
  this->flush();
}

void XMLParserWriter::onAddAttribute(const std::string& name, const std::string& value) {
  // SkASSERT(fElems.empty() || (!fElems.back()->fHasChildren && !fElems.back()->fHasText));
  fParser.addAttribute(name.c_str(), value.c_str());
}

void XMLParserWriter::onAddText(const std::string& text) {
  fParser.text(text.c_str(), static_cast<int>(text.size()));
}

void XMLParserWriter::onEndElement() {
  Elem elem = this->getEnd();
  fParser.endElement(elem.name.c_str());
  this->doEnd();
}

void XMLParserWriter::onStartElement(const std::string& element) {
  this->doStart(element);
  fParser.startElement(element.c_str());
}

}  // namespace tgfx