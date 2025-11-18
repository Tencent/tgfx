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

#include "XMLWriter.h"
#include <cstring>
#include <string>
#include <utility>
#include "XMLParser.h"
#include "core/utils/Log.h"
#include "svg/SVGUtils.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

XMLWriter::XMLWriter(bool doEscapeFlag) : _doEscapeFlag(doEscapeFlag) {
}

XMLWriter::~XMLWriter() {
  ASSERT(_elementsStack.empty());
}

void XMLWriter::flush() {
  while (!_elementsStack.empty()) {
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
  this->addAttribute(name, FloatToString(value));
}

void XMLWriter::addText(const std::string& text) {
  if (_elementsStack.empty()) {
    return;
  }
  this->onAddText(text);
  _elementsStack.top().hasText = true;
}

void XMLWriter::doEnd() {
  _elementsStack.pop();
}

bool XMLWriter::doStart(const std::string& elementName) {
  auto level = _elementsStack.size();
  bool firstChild = level > 0 && !_elementsStack.top().hasChildren;
  if (firstChild) {
    _elementsStack.top().hasChildren = true;
  }
  _elementsStack.emplace(elementName);
  return firstChild;
}

const XMLWriter::Elem& XMLWriter::getEnd() {
  return _elementsStack.top();
}

std::string_view XMLWriter::getHeader() {
  return R"(<?xml version="1.0" encoding="utf-8" ?>)";
}

const char* escape_char(char c, char storage[2]) {
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

size_t escape_markup(char dst[], const char src[], size_t length) {
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
  if (_doEscapeFlag) {
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

void write_dom(std::shared_ptr<DOMNode> node, XMLWriter* writer, bool skipRoot) {
  if (!skipRoot) {
    auto element = node->name;
    if (node->type == DOMNodeType::Text) {
      ASSERT(node->countChildren() == 0);
      writer->addText(element);
      return;
    }

    writer->startElement(element);

    for (const DOMAttribute& attr : node->attributes) {
      writer->addAttribute(attr.name, attr.value);
    }
  }

  node = node->getFirstChild();
  while (node) {
    write_dom(node, writer, false);
    node = node->getNextSibling();
  }

  if (!skipRoot) {
    writer->endElement();
  }
}

void XMLWriter::writeDOM(const std::shared_ptr<DOM>& DOM, bool skipRoot) {
  if (DOM) {
    write_dom(DOM->getRootNode(), this, skipRoot);
  }
}

void XMLWriter::writeHeader() {
}

XMLStreamWriter::XMLStreamWriter(std::shared_ptr<WriteStream> writeStream, bool disablePretty)
    : stream(std::move(writeStream)), disablePrettyXML(disablePretty) {
}

XMLStreamWriter::~XMLStreamWriter() {
  this->flush();
  stream->flush();
}

void XMLStreamWriter::onAddAttribute(const std::string& name, const std::string& value) {
  DEBUG_ASSERT(!_elementsStack.top().hasChildren && !_elementsStack.top().hasText);
  stream->writeText(" " + name + "=\"" + value + "\"");
}

void XMLStreamWriter::onAddText(const std::string& text) {
  auto elem = _elementsStack.top();

  if (!elem.hasChildren && !elem.hasText) {
    stream->writeText(">");
    this->newline();
  }

  this->tab(static_cast<int>(_elementsStack.size()) + 1);
  stream->writeText(text);
  this->newline();
}

void XMLStreamWriter::onEndElement() {
  auto element = getEnd();
  doEnd();
  if (element.hasChildren || element.hasText) {
    this->tab(static_cast<int>(_elementsStack.size()));
    stream->writeText("</" + element.name + ">");
  } else {
    stream->writeText("/>");
  }
  this->newline();
}

void XMLStreamWriter::onStartElement(const std::string& element) {
  auto level = _elementsStack.size();
  if (this->doStart(element)) {
    // the first child, need to close with '>'
    stream->writeText(">");
    this->newline();
  }

  this->tab(static_cast<int>(level));
  stream->writeText("<" + element);
}

void XMLStreamWriter::writeHeader() {
  auto header = getHeader();
  stream->writeText(std::string(header));
  this->newline();
}

void XMLStreamWriter::newline() {
  if (!disablePrettyXML) {
    stream->writeText("\n");
  }
}

void XMLStreamWriter::tab(int level) {
  if (!disablePrettyXML) {
    for (int i = 0; i < level; i++) {
      stream->writeText("\t");
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
XMLParserWriter::XMLParserWriter(XMLParser* parser) : XMLWriter(false), _parser(*parser) {
}

XMLParserWriter::~XMLParserWriter() {
  this->flush();
}

void XMLParserWriter::onAddAttribute(const std::string& name, const std::string& value) {
  DEBUG_ASSERT(_elementsStack.empty() ||
               (!_elementsStack.top().hasChildren && !_elementsStack.top().hasText));
  _parser.addAttribute(name, value);
}

void XMLParserWriter::onAddText(const std::string& text) {
  _parser.text(text);
}

void XMLParserWriter::onEndElement() {
  Elem elem = this->getEnd();
  _parser.endElement(elem.name);
  this->doEnd();
}

void XMLParserWriter::onStartElement(const std::string& element) {
  this->doStart(element);
  _parser.startElement(element);
}

}  // namespace tgfx