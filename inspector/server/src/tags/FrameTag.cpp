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

#include "FrameTag.h"
#include "DataContext.h"
#include "TagUtils.h"

namespace inspector {
void ReadFrameTag(DecodeStream* stream) {
  auto context = dynamic_cast<DataContext*>(stream->context);
  auto& frameData = context->frameData;
  frameData.continuous = stream->readUint8();

  auto frameDataCount = stream->readEncodedUint64();
  frameData.frames.reserve(frameDataCount);
  int64_t refTime = 0;
  if (frameData.continuous) {
    for (uint64_t j = 0; j < frameDataCount; ++j) {
      auto frame = FrameEvent{};
      frame.start = ReadTimeOffset(stream, refTime);
      frame.end = -1;
      frame.drawCall = stream->readEncodedInt64();
      frame.triangles = stream->readEncodedInt64();
      frameData.frames.push_back(frame);
    }
  } else {
    for (uint64_t j = 0; j < frameDataCount; ++j) {
      auto frame = FrameEvent{};
      frame.start = ReadTimeOffset(stream, refTime);
      frame.end = ReadTimeOffset(stream, refTime);
      frame.drawCall = stream->readEncodedInt64();
      frame.triangles = stream->readEncodedInt64();
      frameData.frames.push_back(frame);
    }
  }
}

TagType WriteFrameTag(EncodeStream* stream, const FrameData* frameData) {
  int64_t refTime = 0;
  stream->writeUint8(frameData->continuous);

  stream->writeEncodedUint64(frameData->frames.size());
  if (frameData->continuous) {
    for (const auto& frame : frameData->frames) {
      WriteTimeOffset(stream, refTime, frame.start);
      stream->writeEncodedInt64(frame.drawCall);
      stream->writeEncodedInt64(frame.triangles);
    }
  } else {
    for (const auto& frame : frameData->frames) {
      WriteTimeOffset(stream, refTime, frame.start);
      WriteTimeOffset(stream, refTime, frame.end);
      stream->writeEncodedInt64(frame.drawCall);
      stream->writeEncodedInt64(frame.triangles);
    }
  }
  return TagType::Frame;
}
}  // namespace inspector
