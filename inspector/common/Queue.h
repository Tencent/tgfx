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
namespace inspector {
enum class QueueType : uint8_t { OperateBegin, OperateEnd, KeepAlive, FrameMarkMsg };

#pragma pack(push, 1)

struct QueueHeader {
  union {
    QueueType type;
    uint8_t idx;
  };
};

struct QueueOperaterBase {
  int64_t time;
};

struct QueueOperateBegin: QueueOperaterBase {
  uint8_t type;
};

struct QueueOperateEnd: QueueOperaterBase {
  uint8_t type;
};

struct QueueFrameMark {
  int64_t time;
};

struct QueueItem {
  QueueHeader hdr;
  union {
    QueueFrameMark frameMark;
    QueueOperateBegin operateBegin;
    QueueOperateEnd operateEnd;
  };
};

#pragma pack(pop)

enum { QueueItemSize = sizeof(QueueItem) };
static constexpr size_t QueueDataSize[] = {sizeof(QueueHeader) + sizeof(QueueOperateBegin),
                                           sizeof(QueueHeader) + sizeof(QueueOperateEnd),
                                           sizeof(QueueHeader),
                                           sizeof(QueueHeader) + sizeof(QueueFrameMark)};
}  // namespace inspector