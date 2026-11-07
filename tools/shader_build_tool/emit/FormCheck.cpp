/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "FormCheck.h"

#include <regex>

namespace tgfx {

namespace {

// A word-boundary hazard: variable names like `offset = ` contain the substring "set = ", so
// the leak check must see the layout-qualifier context — the token is preceded by '(' or ','.
// Returns the offset of the first qualifier occurrence, or npos when there is none.
size_t FindLayoutMember(const std::string& text, const char* member) {
  std::string token = std::string(member) + " ";
  size_t cursor = 0;
  while ((cursor = text.find(token, cursor)) != std::string::npos) {
    size_t before = cursor;
    while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) {
      --before;
    }
    if (before > 0 && (text[before - 1] == '(' || text[before - 1] == ',')) {
      return cursor;
    }
    ++cursor;
  }
  return std::string::npos;
}

}  // namespace

DirectFormLeak FindDirectFormLeak(const std::string& text, bool esForm) {
  static const std::regex conditional(
      R"((^|\n)[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif|define)\b)");
  bool headerOk =
      esForm ? (text.find("#version 300 es") != std::string::npos &&
                text.find("precision mediump float;") != std::string::npos)
             : text.find("#version 330") != std::string::npos;
  if (!headerOk) {
    return {"dialect header", 0};
  }
  auto setOffset = FindLayoutMember(text, "set =");
  if (setOffset != std::string::npos) {
    return {"surviving set", setOffset};
  }
  auto bindingOffset = FindLayoutMember(text, "binding =");
  if (bindingOffset != std::string::npos) {
    return {"surviving binding", bindingOffset};
  }
  // The runtime-overridable spec value defaults the emitter writes (`#ifndef TGFX_SPEC_<id> /
  // #define TGFX_SPEC_<id> 0 / #endif`) are the one sanctioned conditional: the runtime
  // specializes the text by inserting `#define` lines ahead of them (MacroPrefixInjector), so
  // the preprocessor folds every accessor before the driver's compiler parses the code. Every
  // other conditional is a leak of the unpreprocessed form.
  static const std::regex specDefaultBlock(
      R"((^|\n)#ifndef TGFX_SPEC_[0-9]+\n#define TGFX_SPEC_[0-9]+ 0\n#endif)");
  std::string cleaned = std::regex_replace(text, specDefaultBlock, "$1");
  // The tool-generated gate constructs (ApplyTextSpecGates): the `#define TGFX_GATE_<name> ...`
  // header lines and the `#if TGFX_GATE_<name> / #else / #endif` wrapper around a gated body.
  // A line-oriented pass drops them; the bodies they wrap are ordinary preprocessed code.
  {
    std::string stripped;
    stripped.reserve(cleaned.size());
    size_t gateDepth = 0;
    size_t cursor = 0;
    while (cursor <= cleaned.size()) {
      auto lineEnd = cleaned.find('\n', cursor);
      const auto line = cleaned.substr(
          cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor);
      const auto head = line.find_first_not_of(" \t");
      const auto trimmedHead = head == std::string::npos ? std::string() : line.substr(head);
      bool drop = false;
      if (gateDepth > 0) {
        if (trimmedHead.rfind("#endif", 0) == 0) {
          --gateDepth;
          drop = true;
        } else if (trimmedHead.rfind("#if", 0) == 0) {
          ++gateDepth;
          drop = true;
        } else if (gateDepth == 1 && trimmedHead.rfind("#else", 0) == 0) {
          drop = true;
        }
      } else if (trimmedHead.rfind("#define TGFX_GATE_", 0) == 0) {
        drop = true;
      } else if (trimmedHead.rfind("#if", 0) == 0 &&
                 trimmedHead.find("TGFX_GATE_") != std::string::npos) {
        gateDepth = 1;
        drop = true;
      }
      if (!drop) {
        stripped += line;
        stripped += '\n';
      }
      if (lineEnd == std::string::npos) {
        break;
      }
      cursor = lineEnd + 1;
    }
    cleaned = std::move(stripped);
  }
  if (std::regex_search(cleaned, conditional)) {
    return {"surviving conditional", 0};
  }
  if (text.find("subpassInput") != std::string::npos ||
      text.find("subpassLoad") != std::string::npos) {
    return {"subpass syntax", 0};
  }
  return {};
}

}  // namespace tgfx
