/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TagHeader.h"
#include "DataContext.h"
namespace inspector {

TagHeader ReadTagHeader(DecodeStream* stream) {
  auto codeAndLength = stream->readUint16();
  uint32_t length = codeAndLength & static_cast<uint8_t>(63);
  uint16_t code = codeAndLength >> 6;
  if (length == 63) {
    length = stream->readUint32();
  }

  TagHeader header = {static_cast<TagType>(code), length};
  return header;
}

void ReadTags(DecodeStream* stream, void (*reader)(DecodeStream*, TagType)) {
  auto header = ReadTagHeader(stream);
  if (stream->context->hasException()) {
    return;
  }
  while (header.type != TagType::End) {
    auto tagBytes = stream->readBytes(header.length);
    reader(&tagBytes, header.type);
    if (stream->context->hasException()) {
      return;
    }
    header = ReadTagHeader(stream);
    if (stream->context->hasException()) {
      return;
    }
  }
}

void WriteTypeAndLength(EncodeStream* stream, TagType code, uint32_t length) {
  auto typeAndLength = static_cast<uint16_t>(code << 6);
  if (length < 63) {
    typeAndLength = typeAndLength | static_cast<uint8_t>(length);
    stream->writeUint16(typeAndLength);
  } else {
    typeAndLength = typeAndLength | static_cast<uint8_t>(63);
    stream->writeUint16(typeAndLength);
    stream->writeUint32(length);
  }
}

void WriteTagHeader(EncodeStream* stream, EncodeStream* tagBytes, TagType code) {
  WriteTypeAndLength(stream, code, tagBytes->length());
  stream->writeBytes(tagBytes);
}

void WriteEndTag(EncodeStream* stream) {
  stream->writeUint16(0);
}

}  // namespace inspector