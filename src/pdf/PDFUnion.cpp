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

#include "PDFUnion.h"
#include "core/utils/Log.h"
#include "pdf/PDFTypes.h"
#include "pdf/PDFUtils.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {
namespace {

// Given an arbitrary string, write it as a valid name (not including leading slash).
void WriteNameEscaped(const std::shared_ptr<WriteStream>& stream, const char* name) {
  static const char kToEscape[] = "#/%()<>[]{}";
  for (auto n = reinterpret_cast<const uint8_t*>(name); *n; ++n) {
    uint8_t v = *n;
    if (v < '!' || v > '~' || strchr(kToEscape, v)) {
      char buffer[3] = {'#', HexadecimalDigits::upper[v >> 4], HexadecimalDigits::upper[v & 0xF]};
      stream->write(buffer, sizeof(buffer));
    } else {
      stream->write(n, 1);
    }
  }
}

void WriteLiteralByteString(const std::shared_ptr<WriteStream>& stream, const char* data,
                            size_t length) {
  stream->writeText("(");
  for (size_t i = 0; i < length; i++) {
    auto c = static_cast<uint8_t>(data[i]);
    if (c < ' ' || '~' < c) {
      uint8_t octal[4] = {'\\', static_cast<uint8_t>('0' | (c >> 6)),
                          static_cast<uint8_t>('0' | ((c >> 3) & 0x07)),
                          static_cast<uint8_t>('0' | (c & 0x07))};
      stream->write(octal, 4);
    } else {
      if (c == '\\' || c == '(' || c == ')') {
        stream->writeText("\\");
      }
      stream->write(&c, 1);
    }
  }
  stream->writeText(")");
}

void WriteHexByteString(const std::shared_ptr<WriteStream>& stream, const char* data,
                        size_t length) {
  stream->writeText("<");
  for (size_t i = 0; i < length; i++) {
    auto c = static_cast<uint8_t>(data[i]);
    char hexValue[2] = {HexadecimalDigits::upper[c >> 4], HexadecimalDigits::upper[c & 0xF]};
    stream->write(hexValue, 2);
  }
  stream->writeText(">");
}

void WriteOptimizedByteString(const std::shared_ptr<WriteStream>& stream, const char* data,
                              size_t length, size_t literalExtras) {
  const size_t hexLength = 2 + (2 * length);
  const size_t literalLength = 2 + length + literalExtras;
  if (literalLength <= hexLength) {
    WriteLiteralByteString(stream, data, length);
  } else {
    WriteHexByteString(stream, data, length);
  }
}

void WriteByteString(const std::shared_ptr<WriteStream>& stream, const char* data, size_t length) {
  size_t literalExtras = 0;
  for (size_t i = 0; i < length; i++) {
    auto c = static_cast<uint8_t>(data[i]);
    if (c < ' ' || '~' < c) {
      literalExtras += 3;
    } else if (c == '\\' || c == '(' || c == ')') {
      ++literalExtras;
    }
  }
  WriteOptimizedByteString(stream, data, length, literalExtras);
}

void WriteTextString(const std::shared_ptr<WriteStream>& stream, const char* data, size_t length) {
  bool inputIsValidUTF8 = true;
  bool inputIsPDFDocEncoding = true;
  size_t literalExtras = 0;
  {
    const char* textStart = data;
    const char* textEnd = data + length;
    while (textStart < textEnd) {
      Unichar unichar = UTF::NextUTF8(&textStart, textEnd);
      if (unichar < 0) {
        inputIsValidUTF8 = false;
        break;
      }
      // See Table D.2 (PDFDocEncoding Character Set) in the PDF3200_2008 spec.
      // Could convert from UTF-8 to PDFDocEncoding and, if successful, use that.
      if ((0x15 < unichar && unichar < 0x20) || 0x7E < unichar) {
        inputIsPDFDocEncoding = false;
        break;
      }
      if (unichar < ' ' || '~' < unichar) {
        literalExtras += 3;
      } else if (unichar == '\\' || unichar == '(' || unichar == ')') {
        ++literalExtras;
      }
    }
  }

  if (!inputIsValidUTF8) {
    LOGE("Invalid UTF8: %.*s\n", (int)length, data);
    stream->writeText("<>");
    return;
  }

  if (inputIsPDFDocEncoding) {
    WriteOptimizedByteString(stream, data, length, literalExtras);
    return;
  }

  stream->writeText("<FEFF");
  const char* textPtr = data;
  const char* textEnd = data + length;
  while (textPtr < textEnd) {
    Unichar unichar = UTF::NextUTF8(&textPtr, textEnd);
    PDFUtils::WriteUTF16beHex(stream, unichar);
  }
  stream->writeText(">");
}
}  // namespace

PDFUnion::PDFUnion(Type type, int32_t value) : intValue(value), type(type) {
}
PDFUnion::PDFUnion(Type type, bool value) : boolValue(value), type(type) {
}
PDFUnion::PDFUnion(Type type, float value) : floatValue(value), type(type) {
}
PDFUnion::PDFUnion(Type type, const char* value) : staticString(value), type(type) {
}
PDFUnion::PDFUnion(Type type, std::string value) : stringValue(std::move(value)), type(type) {
}
PDFUnion::PDFUnion(Type type, std::unique_ptr<PDFObject> value)
    : object(std::move(value)), type(type) {
}

PDFUnion::~PDFUnion() {
  switch (type) {
    case Type::NameString:
    case Type::ByteString:
    case Type::TextString:
      stringValue.~basic_string();
      return;
    case Type::Object:
      object.~unique_ptr();
      return;
    default:
      return;
  }
}

PDFUnion::PDFUnion(PDFUnion&& that) noexcept : type(that.type) {
  DEBUG_ASSERT(this != &that);

  switch (type) {
    case Type::Destroyed:
      break;
    case Type::Int:
    case Type::ColorComponent:
    case Type::Ref:
      intValue = that.intValue;
      break;
    case Type::Bool:
      boolValue = that.boolValue;
      break;
    case Type::Float:
      floatValue = that.floatValue;
      break;
    case Type::NameStaticString:
    case Type::ByteStaticString:
    case Type::TextStaticString:
      staticString = that.staticString;
      break;
    case Type::NameString:
    case Type::ByteString:
    case Type::TextString:
      new (&stringValue) std::string(std::move(that.stringValue));
      break;
    case Type::Object:
      new (&object) std::unique_ptr<PDFObject>(std::move(that.object));
      break;
    default:
      LOGE("PDFUnion::PDFUnion with undefined type");
  }
  that.type = Type::Destroyed;
}

PDFUnion& PDFUnion::operator=(PDFUnion&& that) noexcept {
  if (this != &that) {
    this->~PDFUnion();
    new (this) PDFUnion(std::move(that));
  }
  return *this;
}

bool PDFUnion::isName() const {
  return Type::NameStaticString == type || Type::NameString == type;
}

void PDFUnion::emitObject(const std::shared_ptr<WriteStream>& stream) const {
  switch (type) {
    case Type::Int:
      stream->writeText(std::to_string(intValue));
      return;
    case Type::ColorComponent:
      PDFUtils::AppendColorComponent(static_cast<uint8_t>(intValue), stream);
      return;
    case Type::Bool:
      stream->writeText(boolValue ? "true" : "false");
      return;
    case Type::Float:
      stream->writeText(std::to_string(floatValue));
      return;
    case Type::NameStaticString:
      stream->writeText("/");
      stream->writeText(staticString);
      return;
    case Type::ByteStaticString:
      WriteByteString(stream, staticString, std::strlen(staticString));
      return;
    case Type::TextStaticString:
      WriteTextString(stream, staticString, std::strlen(staticString));
      return;
    case Type::NameString:
      stream->writeText("/");
      WriteNameEscaped(stream, stringValue.c_str());
      return;
    case Type::ByteString:
      WriteByteString(stream, stringValue.c_str(), stringValue.size());
      return;
    case Type::TextString:
      WriteTextString(stream, stringValue.c_str(), stringValue.size());
      return;
    case Type::Object:
      object->emitObject(stream);
      return;
    case Type::Ref:
      stream->writeText(std::to_string(intValue));
      stream->writeText(" 0 R");  // Generation number is always 0.
      return;
    default:
      LOGE("PDFUnion::emitObject with bad type");
  }
}

PDFUnion PDFUnion::Int(int32_t value) {
  return PDFUnion(Type::Int, value);
}

PDFUnion PDFUnion::ColorComponent(uint8_t value) {
  return PDFUnion(Type::ColorComponent, static_cast<int32_t>(value));
}

PDFUnion PDFUnion::Bool(bool value) {
  return PDFUnion(Type::Bool, value);
}

PDFUnion PDFUnion::Float(float value) {
  return PDFUnion(Type::Float, value);
}

PDFUnion PDFUnion::Name(const char* value) {
  DEBUG_ASSERT(value);
  return PDFUnion(Type::NameStaticString, value);
}

PDFUnion PDFUnion::ByteString(const char* value) {
  DEBUG_ASSERT(value);
  return PDFUnion(Type::ByteStaticString, value);
}

PDFUnion PDFUnion::TextString(const char* value) {
  DEBUG_ASSERT(value);
  return PDFUnion(Type::TextStaticString, value);
}

PDFUnion PDFUnion::Name(std::string value) {
  return PDFUnion(Type::NameString, std::move(value));
}

PDFUnion PDFUnion::ByteString(std::string value) {
  return PDFUnion(Type::ByteString, std::move(value));
}

PDFUnion PDFUnion::TextString(std::string value) {
  return PDFUnion(Type::TextString, std::move(value));
}

PDFUnion PDFUnion::Object(std::unique_ptr<PDFObject> object) {
  DEBUG_ASSERT(object.get());
  return PDFUnion(Type::Object, std::move(object));
}

PDFUnion PDFUnion::Ref(PDFIndirectReference ref) {
  DEBUG_ASSERT(ref.value > 0);
  return PDFUnion(Type::Ref, static_cast<int32_t>(ref.value));
}

}  // namespace tgfx
