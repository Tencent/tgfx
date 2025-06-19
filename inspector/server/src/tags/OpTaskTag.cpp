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

#include "OpTaskTag.h"
#include "DataContext.h"

namespace inspector {
void ReadOpTaskTag(DecodeStream* stream) {
  auto context = dynamic_cast<DataContext*>(stream->context);
  context->baseTime = stream->readEncodedInt64();
  context->lastTime = stream->readEncodedInt64();

  auto count = stream->readEncodedUint64();
  auto& opTasks = context->opTasks;
  opTasks.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    auto ptr = std::make_shared<OpTaskData>();
    ptr->start = stream->readEncodedInt64();
    ptr->end = stream->readEncodedInt64();
    ptr->type = stream->readUint8();
    ptr->id = i;
    opTasks.push_back(ptr);
  }

  count = stream->readEncodedUint64();
  auto& opChilds = context->opChilds;
  opChilds.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    auto parentIndex = stream->readEncodedUint32();
    auto childCount = stream->readEncodedUint32();
    std::vector<uint32_t> childs;
    childs.reserve(childCount);
    for (uint32_t j = 0; j < childCount; ++j) {
      auto childIndex = stream->readEncodedUint32();
      childs.push_back(childIndex);
    }
    opChilds[parentIndex] = childs;
  }
}

TagType WriteOpTaskTag(EncodeStream* stream, DataContext* context) {
  stream->writeEncodedInt64(context->baseTime);
  stream->writeEncodedInt64(context->lastTime);

  const auto& opTasks = context->opTasks;
  stream->writeEncodedUint64(opTasks.size());
  for (const auto& opTask : opTasks) {
    stream->writeEncodedInt64(opTask->start);
    stream->writeEncodedInt64(opTask->end);
    stream->writeUint8(opTask->type);
  }

  const auto& opChilds = context->opChilds;
  stream->writeEncodedUint64(opChilds.size());
  for (const auto& opChild : opChilds) {
    auto& childs = opChild.second;
    stream->writeEncodedUint32(opChild.first);
    stream->writeEncodedUint32(static_cast<uint32_t>(childs.size()));
    for (const auto& child : childs) {
      stream->writeEncodedUint32(child);
    }
  }
  return TagType::OpTask;
}

}  // namespace inspector