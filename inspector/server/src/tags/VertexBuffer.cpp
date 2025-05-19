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

#include "DataContext.h"
#include "DecodeStream.h"
#include "VertexBufferTag.h"

namespace inspector {
void ReadVertexBufferTag(DecodeStream* stream) {
  auto count = stream->readEncodedUint32();
  auto context = dynamic_cast<DataContext*>(stream->context);
  auto& vertexBuffer = context->vertexDatas;
  for(uint32_t i = 0; i < count; ++i) {
    auto childIndex = stream->readEncodedUint32();

    auto ptr = std::make_shared<VertexData>();
    auto vertexCount = stream->readEncodedUint32();
    stream->readFloatList(ptr->vertexData.data(), vertexCount, SPATIAL_PRECISION);
    ptr->hasUV = stream->readBoolean();
    ptr->hasColor = stream->readBoolean();

    vertexBuffer[childIndex] = ptr;
  }
}

TagType WriteVertexBufferTag(EncodeStream* stream, std::unordered_map<uint32_t, std::shared_ptr<VertexData>>* vertexDatas) {
  stream->writeEncodedUint32(static_cast<uint32_t>(vertexDatas->size()));
  for (const auto& vertexBuffer: *vertexDatas) {
    stream->writeEncodedUint32(vertexBuffer.first);

    auto vertexData = vertexBuffer.second;
    auto vertexDataCount = static_cast<uint32_t>(vertexData->vertexData.size());
    stream->writeEncodedUint32(vertexDataCount);
    stream->writeFloatList(vertexData->vertexData.data(), vertexDataCount, SPATIAL_PRECISION);
    stream->writeBoolean(vertexData->hasUV);
    stream->writeBoolean(vertexData->hasColor);
  }
  return TagType::VertexBuffer;
}

}