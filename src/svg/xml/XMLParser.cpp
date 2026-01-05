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

#include "XMLParser.h"
#include <cstddef>
#include <cstdlib>
#include <string>
#include "core/utils/Log.h"
#include "expat/lib/expat.h"

namespace tgfx {

namespace {
template <typename T, T* P>
struct OverloadedFunctionObject {
  template <typename... Args>
  auto operator()(Args&&... args) const -> decltype(P(std::forward<Args>(args)...)) {
    return P(std::forward<Args>(args)...);
  }
};

template <auto F>
using FunctionObject = OverloadedFunctionObject<std::remove_pointer_t<decltype(F)>, F>;

template <typename T, void (*P)(T*)>
class AutoTCallVProc : public std::unique_ptr<T, FunctionObject<P>> {
  using inherited = std::unique_ptr<T, FunctionObject<P>>;

 public:
  using inherited::inherited;
  AutoTCallVProc(const AutoTCallVProc&) = delete;
  AutoTCallVProc(AutoTCallVProc&& that) noexcept : inherited(std::move(that)) {
  }

  operator T*() const {
    return this->get();
  }
};

constexpr const void* HASH_SEED = &HASH_SEED;

const XML_Memory_Handling_Suite XML_alloc = {malloc, realloc, free};

struct ParsingContext {
  explicit ParsingContext(XMLParser* parser)
      : _parser(parser), _XMLParser(XML_ParserCreate_MM(nullptr, &XML_alloc, nullptr)) {
  }

  void flushText() {
    if (!_bufferedText.empty()) {
      _parser->text(_bufferedText);
      _bufferedText.clear();
    }
  }

  void appendText(const char* txt, size_t len) {
    _bufferedText.insert(_bufferedText.end(), txt, &txt[len]);
  }

  XMLParser* _parser;
  AutoTCallVProc<std::remove_pointer_t<XML_Parser>, XML_ParserFree> _XMLParser;

 private:
  std::string _bufferedText;
};

#define HANDLER_CONTEXT(arg, name) ParsingContext* name = static_cast<ParsingContext*>(arg)

void XMLCALL start_element_handler(void* data, const char* tag, const char** attributes) {
  HANDLER_CONTEXT(data, context);
  context->flushText();

  context->_parser->startElement(tag);

  for (size_t i = 0; attributes[i]; i += 2) {
    context->_parser->addAttribute(attributes[i], attributes[i + 1]);
  }
}

void XMLCALL end_element_handler(void* data, const char* tag) {
  HANDLER_CONTEXT(data, context);
  context->flushText();

  context->_parser->endElement(tag);
}

void XMLCALL text_handler(void* data, const char* txt, int len) {
  HANDLER_CONTEXT(data, context);

  context->appendText(txt, static_cast<size_t>(len));
}

void XMLCALL entity_decl_handler(void* data, const XML_Char* entityName,
                                 int /*is_parameter_entity*/, const XML_Char* /*value*/,
                                 int /*value_length*/, const XML_Char* /*base*/,
                                 const XML_Char* /*systemId*/, const XML_Char* /*publicId*/,
                                 const XML_Char* /*notationName*/) {
  HANDLER_CONTEXT(data, context);

  LOGE("'%s' entity declaration found, stopping processing", entityName);
  XML_StopParser(context->_XMLParser, XML_FALSE);
}

}  // anonymous namespace

XMLParser::XMLParser() = default;
XMLParser::~XMLParser() = default;

bool XMLParser::parse(Stream& stream) {
  ParsingContext parsingContext(this);
  if (!parsingContext._XMLParser) {
    LOGE("could not create XML parser\n");
    return false;
  }

  // Avoid calls to rand_s if this is not set. This seed helps prevent DOS
  // with a known hash sequence so an address is sufficient. The provided
  // seed should not be zero as that results in a call to rand_s.
  auto seed = static_cast<unsigned long>(reinterpret_cast<size_t>(HASH_SEED) & 0xFFFFFFFF);
  XML_SetHashSalt(parsingContext._XMLParser, seed ? seed : 1);

  XML_SetUserData(parsingContext._XMLParser, &parsingContext);
  XML_SetElementHandler(parsingContext._XMLParser, start_element_handler, end_element_handler);
  XML_SetCharacterDataHandler(parsingContext._XMLParser, text_handler);

  // Disable entity processing, to inhibit internal entity expansion. See expat CVE-2013-0340.
  XML_SetEntityDeclHandler(parsingContext._XMLParser, entity_decl_handler);

  XML_Status status = XML_STATUS_OK;
  if (stream.getMemoryBase() && stream.size() != 0) {
    const char* base = reinterpret_cast<const char*>(stream.getMemoryBase());
    status = XML_Parse(parsingContext._XMLParser, base, static_cast<int>(stream.size()), true);
  } else {
    static constexpr int BUFFER_SIZE = 4096;
    bool done = false;
    size_t length = stream.size();
    size_t currentPos = 0;
    do {
      void* buffer = XML_GetBuffer(parsingContext._XMLParser, BUFFER_SIZE);
      if (!buffer) {
        return false;
      }

      size_t readLength = stream.read(buffer, BUFFER_SIZE);
      currentPos += readLength;
      done = currentPos >= length;
      status = XML_ParseBuffer(parsingContext._XMLParser, static_cast<int>(readLength), done);
      if (XML_STATUS_ERROR == status) {
        break;
      }
    } while (!done);
  }
  return XML_STATUS_ERROR != status;
}

bool XMLParser::startElement(const std::string& element) {
  return this->onStartElement(element);
}

bool XMLParser::addAttribute(const std::string& name, const std::string& value) {
  return this->onAddAttribute(name, value);
}

bool XMLParser::endElement(const std::string& element) {
  return this->onEndElement(element);
}

bool XMLParser::text(const std::string& text) {
  return this->onText(text);
}

}  // namespace tgfx
