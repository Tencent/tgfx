/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/WriteStream.h"
namespace tgfx {

class PDFObject;
struct PDFIndirectReference;

class PDFUnion {
 public:
  PDFUnion& operator=(const PDFUnion&) = delete;
  PDFUnion(const PDFUnion&) = delete;
  PDFUnion(PDFUnion&&) noexcept;
  PDFUnion& operator=(PDFUnion&&) noexcept;

  ~PDFUnion();

  static PDFUnion Int(int value);

  static PDFUnion Int(size_t value) {
    return PDFUnion::Int(static_cast<int>(value));
  }

  static PDFUnion Bool(bool value);

  static PDFUnion Float(float value);

  static PDFUnion ColorComponent(uint8_t value);

  static PDFUnion Name(const char* value);

  static PDFUnion ByteString(const char* value);

  static PDFUnion TextString(const char* value);

  static PDFUnion Name(std::string value);

  static PDFUnion ByteString(std::string value);

  static PDFUnion TextString(std::string value);

  static PDFUnion Object(std::unique_ptr<PDFObject> object);

  static PDFUnion Ref(PDFIndirectReference ref);

  void emitObject(const std::shared_ptr<WriteStream>& stream) const;

  bool isName() const;

 private:
  union {
    int32_t intValue;
    bool boolValue;
    float floatValue;
    const char* staticString;
    std::string stringValue;
    std::unique_ptr<PDFObject> object;
  };

  enum class Type {
    Destroyed = 0,
    Int,
    ColorComponent,
    Bool,
    Float,
    NameStaticString,
    ByteStaticString,
    TextStaticString,
    NameString,
    ByteString,
    TextString,
    Object,
    Ref,
  };

  Type type;

  PDFUnion(Type type, int32_t value);
  PDFUnion(Type type, bool value);
  PDFUnion(Type type, float value);
  PDFUnion(Type type, const char* value);
  PDFUnion(Type type, std::string value);
  PDFUnion(Type type, std::unique_ptr<PDFObject> value);
};

}  // namespace tgfx
