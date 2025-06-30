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

#include "FileTags.h"
#include <functional>
#include <unordered_map>
#include "DataContext.h"
#include "EnumClassHash.h"
#include "FrameTag.h"
#include "NameMapTag.h"
#include "OpTaskTag.h"
#include "PropertyTag.h"
#include "TextureTag.h"
#include "VertexBufferTag.h"

namespace inspector {
using ReadTagHandler = void(DecodeStream* stream);
static const std::unordered_map<TagType, std::function<ReadTagHandler>, EnumClassHash> readHanders =
    {
        {TagType::NameMap, ReadNameMapTag}, {TagType::Frame, ReadFrameTag},
        {TagType::OpTask, ReadOpTaskTag},   {TagType::Property, ReadPropertyTag},
        {TagType::Texture, ReadTextureTag}, {TagType::VertexBuffer, ReadVertexBufferTag},
};

void ReadTagsOfFile(DecodeStream* stream, TagType type) {
  auto iter = readHanders.find(type);
  if (iter != readHanders.end()) {
    iter->second(stream);
  }
}

void WriteTagsOfFile(EncodeStream* stream) {
  auto context = dynamic_cast<DataContext*>(stream->context);

  const auto& nameMap = context->nameMap;
  if (!nameMap.empty()) {
    WriteTag(stream, &nameMap, WriteNameMapTag);
  }

  const auto& frames = context->frameData;
  WriteTag(stream, &frames, WriteFrameTag);

  auto& opTasks = context->opTasks;
  auto& opChilds = context->opChilds;
  if (!opTasks.empty() || !opChilds.empty()) {
    WriteTag(stream, context, WriteOpTaskTag);
  }

  auto& properties = context->properties;
  if (!properties.empty()) {
    WriteTag(stream, &properties, WritePropertyTag);
  }

  auto& textures = context->textures;
  if (!textures.empty()) {
    WriteTag(stream, &textures, WriteTextureTag);
  }

  auto& vertexBuffer = context->vertexDatas;
  if (!vertexBuffer.empty()) {
    WriteTag(stream, &vertexBuffer, WriteVertexBufferTag);
  }
  WriteEndTag(stream);
}

}  // namespace inspector