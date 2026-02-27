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

#include "PDFTypes.h"
#include "core/utils/Log.h"
#include "pdf/DeflateStream.h"
#include "pdf/PDFDocumentImpl.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Stream.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

size_t PDFArray::size() const {
  return values.size();
}

void PDFArray::reserve(size_t length) {
  values.reserve(length);
}

void PDFArray::emitObject(const std::shared_ptr<WriteStream>& stream) const {
  stream->writeText("[");
  for (size_t i = 0; i < values.size(); i++) {
    values[i].emitObject(stream);
    if (i + 1 < values.size()) {
      stream->writeText(" ");
    }
  }
  stream->writeText("]");
}

void PDFArray::append(PDFUnion&& value) {
  values.emplace_back(std::move(value));
}

void PDFArray::appendInt(int32_t value) {
  this->append(PDFUnion::Int(value));
}

void PDFArray::appendColorComponent(uint8_t value) {
  this->append(PDFUnion::ColorComponent(value));
}

void PDFArray::appendBool(bool value) {
  this->append(PDFUnion::Bool(value));
}

void PDFArray::appendScalar(float value) {
  this->append(PDFUnion::Float(value));
}

void PDFArray::appendName(const char name[]) {
  this->append(PDFUnion::Name(std::string(name)));
}

void PDFArray::appendName(std::string name) {
  this->append(PDFUnion::Name(std::move(name)));
}

void PDFArray::appendTextString(std::string value) {
  this->append(PDFUnion::TextString(std::move(value)));
}

void PDFArray::appendByteString(const char value[]) {
  this->append(PDFUnion::ByteString(value));
}

void PDFArray::appendObject(std::unique_ptr<PDFObject>&& object) {
  this->append(PDFUnion::Object(std::move(object)));
}

void PDFArray::appendRef(PDFIndirectReference ref) {
  this->append(PDFUnion::Ref(ref));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<PDFDictionary> PDFDictionary::Make() {
  return std::make_unique<PDFDictionary>();
}

std::unique_ptr<PDFDictionary> PDFDictionary::Make(std::string type) {
  return std::make_unique<PDFDictionary>(std::move(type));
}

PDFDictionary::PDFDictionary(std::string type) {
  this->insertName("Type", std::move(type));
}

void PDFDictionary::emitObject(const std::shared_ptr<WriteStream>& stream) const {
  stream->writeText("<<");
  for (size_t i = 0; i < records.size(); ++i) {
    const auto& [key, value] = records[i];
    key.emitObject(stream);
    stream->writeText(" ");
    value.emitObject(stream);
    if (i + 1 < records.size()) {
      stream->writeText("\n");
    }
  }
  stream->writeText(">>");
}

size_t PDFDictionary::size() const {
  return records.size();
}

void PDFDictionary::reserve(size_t length) {
  records.reserve(length);
}

void PDFDictionary::insertRef(const char key[], PDFIndirectReference ref) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::Ref(ref));
}

void PDFDictionary::insertRef(std::string key, PDFIndirectReference ref) {
  records.emplace_back(PDFUnion::Name(std::move(key)), PDFUnion::Ref(ref));
}

void PDFDictionary::insertObject(const char key[], std::unique_ptr<PDFObject>&& object) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::Object(std::move(object)));
}
void PDFDictionary::insertObject(std::string key, std::unique_ptr<PDFObject>&& object) {
  records.emplace_back(PDFUnion::Name(std::move(key)), PDFUnion::Object(std::move(object)));
}

void PDFDictionary::insertBool(const char key[], bool value) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::Bool(value));
}

void PDFDictionary::insertInt(const char key[], int32_t value) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::Int(value));
}

void PDFDictionary::insertInt(const char key[], size_t value) {
  this->insertInt(key, static_cast<int>(value));
}

void PDFDictionary::insertScalar(const char key[], float value) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::Float(value));
}

void PDFDictionary::insertName(const char key[], const char name[]) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::Name(name));
}

void PDFDictionary::insertName(const char key[], std::string name) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::Name(std::move(name)));
}

void PDFDictionary::insertByteString(const char key[], const char value[]) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::ByteString(value));
}

void PDFDictionary::insertTextString(const char key[], const char value[]) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::TextString(value));
}

void PDFDictionary::insertByteString(const char key[], std::string value) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::ByteString(std::move(value)));
}

void PDFDictionary::insertTextString(const char key[], std::string value) {
  records.emplace_back(PDFUnion::Name(key), PDFUnion::TextString(std::move(value)));
}

void PDFDictionary::insertUnion(const char key[], PDFUnion&& value) {
  records.emplace_back(PDFUnion::Name(key), std::move(value));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {
bool StreamCopy(WriteStream* out, Stream* input) {
  char scratch[4096];
  size_t count;
  while (true) {
    count = input->read(scratch, sizeof(scratch));
    if (0 == count) {
      return true;
    }
    if (!out->write(scratch, count)) {
      return false;
    }
  }
}

void SerializeStream(PDFDictionary* origDict, Stream* stream, PDFSteamCompressionEnabled compress,
                     PDFDocumentImpl* doc, PDFIndirectReference ref) {
  // Code assumes that the stream starts at the beginning.
  DEBUG_ASSERT(stream);

  auto inputPointer = stream;
  std::shared_ptr<Data> data = {};
  std::unique_ptr<Stream> streamPointer = {};

  PDFDictionary tempDict;
  PDFDictionary& dict = origDict ? *origDict : tempDict;
  static const size_t MinimumSavings = strlen("/Filter_/FlateDecode_");
  if (doc->metadata().compressionLevel != PDFMetadata::CompressionLevel::None &&
      compress == PDFSteamCompressionEnabled::Yes && stream->size() > MinimumSavings) {
    auto compressedData = MemoryWriteStream::Make();
    DeflateWriteStream deflateStream(compressedData.get(),
                                     static_cast<int>(doc->metadata().compressionLevel));
    StreamCopy(&deflateStream, stream);
    deflateStream.finalize();
    if (stream->size() > compressedData->bytesWritten() + MinimumSavings) {
      data = compressedData->readData();
      streamPointer = Stream::MakeFromData(data);
      inputPointer = streamPointer.get();
      dict.insertName("Filter", "FlateDecode");
    } else {
      DEBUG_ASSERT(stream->rewind());
    }
  }

  dict.insertInt("Length", stream->size());

  auto writeStream = [inputPointer](const std::shared_ptr<WriteStream>& destination) {
    StreamCopy(destination.get(), inputPointer);
  };
  doc->emitStream(dict, writeStream, ref);
}

void WriteLiteralByteString(const std::shared_ptr<WriteStream>& stream, const char* cin,
                            size_t len) {
  stream->writeText("(");
  for (size_t i = 0; i < len; i++) {
    auto c = static_cast<uint8_t>(cin[i]);
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

void WriteHexByteString(const std::shared_ptr<WriteStream>& stream, const char* cin, size_t len) {

  stream->writeText("<");
  for (size_t i = 0; i < len; i++) {
    auto c = static_cast<uint8_t>(cin[i]);
    char hexValue[2] = {HexadecimalDigits::upper[c >> 4], HexadecimalDigits::upper[c & 0xF]};
    stream->write(hexValue, 2);
  }
  stream->writeText(">");
}

void WriteOptimizedByteString(const std::shared_ptr<WriteStream>& stream, const char* cin,
                              size_t len, size_t literalExtras) {
  const size_t hexLength = 2 + (2 * len);
  const size_t literalLength = 2 + len + literalExtras;
  if (literalLength <= hexLength) {
    WriteLiteralByteString(stream, cin, len);
  } else {
    WriteHexByteString(stream, cin, len);
  }
}

void WriteTextString(const std::shared_ptr<WriteStream>& stream, const char* cin, size_t len) {

  bool inputIsValidUTF8 = true;
  bool inputIsPDFDocEncoding = true;
  size_t literalExtras = 0;
  {
    const char* textPtr = cin;
    const char* textEnd = cin + len;
    while (textPtr < textEnd) {
      Unichar unichar = UTF::NextUTF8(&textPtr, textEnd);
      if (unichar < 0) {
        inputIsValidUTF8 = false;
        break;
      }
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
    stream->writeText("<>");
    return;
  }

  if (inputIsPDFDocEncoding) {
    WriteOptimizedByteString(stream, cin, len, literalExtras);
    return;
  }

  stream->writeText("<FEFF");
  const char* textPtr = cin;
  const char* textEnd = cin + len;
  while (textPtr < textEnd) {
    Unichar unichar = UTF::NextUTF8(&textPtr, textEnd);
    PDFUtils::WriteUTF16beHex(stream, unichar);
  }
  stream->writeText(">");
}
}  // namespace

PDFIndirectReference PDFStreamOut(std::unique_ptr<PDFDictionary> dict,
                                  std::unique_ptr<Stream> stream, PDFDocumentImpl* doc,
                                  PDFSteamCompressionEnabled compress) {
  PDFIndirectReference ref = doc->reserveRef();
  SerializeStream(dict.get(), stream.get(), compress, doc, ref);
  return ref;
}

void PDFWriteTextString(const std::shared_ptr<WriteStream>& stream, const std::string& text) {
  WriteTextString(stream, text.data(), text.size());
}
void PDFWriteByteString(const std::shared_ptr<WriteStream>& stream, const char* bytes,
                        size_t length) {
  WriteTextString(stream, bytes, length);
}

}  // namespace tgfx
