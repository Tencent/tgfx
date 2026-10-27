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
  if (std::regex_search(text, conditional)) {
    return {"surviving conditional", 0};
  }
  if (text.find("subpassInput") != std::string::npos ||
      text.find("subpassLoad") != std::string::npos) {
    return {"subpass syntax", 0};
  }
  return {};
}

}  // namespace tgfx
