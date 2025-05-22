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

#pragma once
#include <cstdint>
#include "DecodeStream.h"
#include "EncodeStream.h"

namespace inspector {
enum TagType : uint8_t {
  End,
  Frame,
  OpTask,
  Property,
  Texture,
  VertexBuffer,
  ShaderAndUniform,
};

struct TagHeader {
  TagType type;
  uint32_t length;
};

TagHeader ReadTagHeader(DecodeStream* stream);
void ReadTags(DecodeStream* stream, void (*reader)(DecodeStream*, TagType));

void WriteTagHeader(EncodeStream* stream, EncodeStream* tagBytes, TagType code);
void WriteEndTag(EncodeStream* stream);

template <typename T>
void WriteTag(EncodeStream* stream, T parameter, TagType (*writer)(EncodeStream*, T)) {
  EncodeStream bytes(stream->context);
  auto code = writer(&bytes, parameter);
  WriteTagHeader(stream, &bytes, code);
}
}  // namespace inspector