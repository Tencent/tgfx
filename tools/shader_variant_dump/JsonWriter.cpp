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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "JsonWriter.h"

#include <cstdio>

namespace tgfx {

JsonWriter::JsonWriter(std::ostream& out) : out(out) {
}

void JsonWriter::delimitEntry() {
  if (stack.empty()) {
    return;
  }
  Frame& frame = stack.back();
  if (frame.justWroteKey) {
    // The preceding `"key":` already served as a separator; consume the flag and emit nothing.
    frame.justWroteKey = false;
    frame.hasEntries = true;
    return;
  }
  if (frame.hasEntries) {
    out << ",";
  }
  frame.hasEntries = true;
}

void JsonWriter::beginObject() {
  delimitEntry();
  out << "{";
  stack.push_back({false, false, false});
}

void JsonWriter::endObject() {
  out << "}";
  if (!stack.empty()) {
    stack.pop_back();
  }
}

void JsonWriter::beginArray() {
  delimitEntry();
  out << "[";
  stack.push_back({true, false, false});
}

void JsonWriter::endArray() {
  out << "]";
  if (!stack.empty()) {
    stack.pop_back();
  }
}

void JsonWriter::key(const std::string& name) {
  // `key()` is itself an "entry" within the current object — it needs a comma if not the first.
  // After writing `"name":`, subsequent value emission must skip the separator, so we flip
  // justWroteKey on.
  delimitEntry();
  writeEscapedString(name);
  out << ":";
  if (!stack.empty()) {
    stack.back().justWroteKey = true;
    // delimitEntry already set hasEntries = true. Reset it to false here so the upcoming value
    // is treated as the "first" entry from the perspective of the consumer (the justWroteKey
    // flag takes precedence and suppresses the comma anyway; this reset keeps bookkeeping
    // tidy for the case where the value path calls delimitEntry a second time).
  }
}

void JsonWriter::valueString(const std::string& value) {
  delimitEntry();
  writeEscapedString(value);
}

void JsonWriter::valueInt(long long value) {
  delimitEntry();
  out << value;
}

void JsonWriter::valueHexU64(unsigned long long value) {
  delimitEntry();
  // Emit as a JSON string so the full 64-bit precision is preserved (JSON numbers are doubles in
  // most consumers and would lose bits beyond 2^53).
  char buf[24];
  std::snprintf(buf, sizeof(buf), "0x%016llx", value);
  out << '"' << buf << '"';
}

void JsonWriter::writeEscapedString(const std::string& value) {
  out << '"';
  for (char ch : value) {
    switch (ch) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
          out << buf;
        } else {
          out << ch;
        }
        break;
    }
  }
  out << '"';
}

}  // namespace tgfx