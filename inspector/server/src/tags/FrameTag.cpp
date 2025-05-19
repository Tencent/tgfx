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
#include "FrameTag.h"
#include "DataContext.h"

namespace inspector {
int64_t GetFrameTime(DataContext* context, const FrameData& fd, size_t idx) {
  if(fd.continuous) {
    if(idx < fd.frames.size() - 1) {
      return fd.frames[idx+1].start - fd.frames[idx].start;
    }
    assert(context->lastTime != 0);
    return context->lastTime - fd.frames.back().start;
  }
  const auto& frame = fd.frames[idx];
  if(frame.end >= 0) {
    return frame.end - frame.start;
  }
  return context->lastTime - fd.frames.back().start;
}

void ReadFrameTag(DecodeStream* stream) {
  auto count = stream->readEncodedUint32();
  auto context = dynamic_cast<DataContext*>(stream->context);
  auto& frameDatas = context->frames.Data();
  context->frames.Data().reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    auto ptr = new FrameData;
    ptr->name = stream->readEncodedUint64();
    ptr->continuous = stream->readUint8();

    auto frameDataCount = stream->readEncodedUint64();
    ptr->frames.reserve(frameDataCount);
    int64_t refTime = 0;
    if (ptr->continuous) {
      for(uint64_t j = 0; j < frameDataCount; ++j) {
        ptr->frames[j].start = ReadTimeOffset(stream, refTime);
        ptr->frames[j].end = -1;
        ptr->frames[j].drawCall = stream->readEncodedInt64();
        ptr->frames[j].triangles = stream->readEncodedInt64();
      }
    }
    else {
      for(uint64_t j = 0; j < frameDataCount; ++j) {
        ptr->frames[j].start = ReadTimeOffset(stream, refTime);
        ptr->frames[j].end = ReadTimeOffset(stream, refTime);
        ptr->frames[j].drawCall = stream->readEncodedInt64();
        ptr->frames[j].triangles = stream->readEncodedInt64();
      }
    }

    for(uint64_t j = 0; j < frameDataCount; ++j) {
      const auto timeSpan = GetFrameTime(context, *ptr, j);
      if (timeSpan > 0) {
        ptr->min = std::min(ptr->min, timeSpan);
        ptr->max = std::max(ptr->max, timeSpan);
        ptr->total += timeSpan;
        ptr->sumSq += double(timeSpan) * timeSpan;
      }
    }
    frameDatas[i] = ptr;
  }
  context->framebase = frameDatas[0];
}

TagType WriteFrameTag(EncodeStream* stream, StringDiscovery<FrameData*>* frames) {
  stream->writeEncodedUint32(static_cast<uint32_t>(frames->Data().size()));
  for (auto& frameData: frames->Data()) {
    int64_t refTime = 0;
    stream->writeEncodedUint64(frameData->name);
    stream->writeUint8(frameData->continuous);

    stream->writeEncodedUint64(frameData->frames.size());
    if (frameData->continuous) {
      for (auto& frame: frameData->frames) {
        WriteTimeOffset(stream, refTime, frame.start);
        stream->writeEncodedInt64(frame.drawCall);
        stream->writeEncodedInt64(frame.triangles);
      }
    }
    else {
      for (auto& frame: frameData->frames) {
        WriteTimeOffset(stream, refTime, frame.start);
        WriteTimeOffset(stream, refTime, frame.end);
        stream->writeEncodedInt64(frame.drawCall);
        stream->writeEncodedInt64(frame.triangles);
      }
    }
  }
  return TagType::Frame;
}
}
