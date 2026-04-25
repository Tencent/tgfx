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

#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace tgfx {

/**
 * Minimal JSON writer sufficient for the shader_variant_dump tool. Supports hierarchical objects
 * and arrays with comma management, and escapes double quotes / backslashes / newlines / tabs in
 * string values. Not a general-purpose library — only the subset this tool needs.
 */
class JsonWriter {
 public:
  explicit JsonWriter(std::ostream& out);

  /** Opens a `{` and pushes an object context. Emits a leading comma if the parent is an array or
   *  another object and this is not the first entry. */
  void beginObject();
  /** Closes a `}` and pops the current object context. */
  void endObject();

  /** Opens a `[` and pushes an array context. */
  void beginArray();
  /** Closes a `]` and pops the current array context. */
  void endArray();

  /** Writes a `"key": ` prefix in the current object. */
  void key(const std::string& name);

  /** Emits a JSON string literal (with escaping). */
  void valueString(const std::string& value);
  /** Emits a JSON integer literal. */
  void valueInt(long long value);
  /** Emits a JSON hex string (for 64-bit hashes that may exceed double precision). */
  void valueHexU64(unsigned long long value);

 private:
  std::ostream& out;
  /** Stack of frames: each tracks whether entries already exist and whether a key was just
   *  written (in which case the next value must NOT emit a leading comma — the colon already
   *  terminates the key prefix). */
  struct Frame {
    bool isArray = false;
    bool hasEntries = false;
    bool justWroteKey = false;
  };
  std::vector<Frame> stack;

  /** Emit a comma if the current frame already has entries; then mark it as having entries.
   *  Consumes the `justWroteKey` flag: if the caller was writing a value right after a key,
   *  no separating comma is needed (the colon already does that job). */
  void delimitEntry();
  /** Escape a string per JSON rules and write with surrounding quotes. */
  void writeEscapedString(const std::string& value);
};

}  // namespace tgfx