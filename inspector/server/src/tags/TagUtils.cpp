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

#include "TagUtils.h"

namespace inspector {
int64_t ReadTimeOffset(DecodeStream* stream, int64_t& refTime) {
  int64_t timeOffset = stream->readEncodedInt64();
  refTime += timeOffset;
  return refTime;
}

void WriteTimeOffset(EncodeStream* stream, int64_t& refTime, int64_t time) {
  int64_t timeOffset = time - refTime;
  refTime += timeOffset;
  stream->writeEncodedInt64(timeOffset);
}

void ReadDataHead(std::vector<DataHead>& dataHead, DecodeStream* stream) {
  auto dataHeadCount = stream->readEncodedUint32();
  dataHead.reserve(dataHeadCount);
  for (uint32_t j = 0; j < dataHeadCount; ++j) {
    dataHead[j].type = static_cast<DataType>(stream->readUint8());
    dataHead[j].size = stream->readUint16();
  }
}

void WriteDataHead(const std::vector<DataHead>& dataHead, EncodeStream* stream) {
  stream->writeEncodedUint32(static_cast<uint32_t>(dataHead.size()));
  for (const auto& data : dataHead) {
    stream->writeUint8(data.type);
    stream->writeUint16(data.size);
  }
}
}  // namespace inspector