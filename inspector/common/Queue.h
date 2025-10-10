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

#pragma once
#include <cstdint>
namespace inspector {
enum class QueueType : uint8_t {
  OperateBegin,
  OperateEnd,
  FrameMarkMsg,
  ValueDataUint32,
  ValueDataFloat4,
  ValueDataMat4,
  ValueDataInt,
  ValueDataColor,
  ValueDataFloat,
  ValueDataBool,
  ValueDataEnum,
  TextureSampler,
  TextureData,
  KeepAlive,
  StringData,
  ValueName,
  PixelsData,
};

#pragma pack(push, 1)

struct QueueHeader {
  union {
    QueueType type;
    uint8_t idx;
  };
};

struct QueueOperaterBase {
  int64_t nsTime;
};

struct QueueOperateBegin : QueueOperaterBase {
  uint8_t type;
};

struct QueueOperateEnd : QueueOperaterBase {
  uint8_t type;
};

struct QueueFrameMark {
  int64_t nsTime;
};

struct QueueAttributeData {
  uint64_t name;
};

struct QueueAttributeDataUInt32 : QueueAttributeData {
  uint32_t value;
};

struct QueueAttributeDataFloat4 : QueueAttributeData {
  float value[4];
};

struct QueueAttributeDataMat4 : QueueAttributeData {
  float value[6];
};

struct QueueAttributeDataInt : QueueAttributeData {
  int value;
};

struct QueueAttributeDataFloat : QueueAttributeData {
  float value;
};

struct QueueAttributeDataBool : QueueAttributeData {
  bool value;
};

struct QueueAttributeDataEnum : QueueAttributeData {
  uint16_t value;
};

struct QueueStringTransfer {
  uint64_t ptr;
};

struct QueueTextureSampler {
  uint64_t samplerPtr;
};

struct QueueTextureData : QueueTextureSampler {
  uint8_t format;
  int width;
  int height;
  size_t rowBytes;
  uint64_t pixels;
};

struct QueueItem {
  QueueHeader hdr;
  union {
    QueueFrameMark frameMark;
    QueueOperateBegin operateBegin;
    QueueOperateEnd operateEnd;
    QueueStringTransfer stringTransfer;
    QueueAttributeDataUInt32 attributeDataUint32;
    QueueAttributeDataFloat4 attributeDataFloat4;
    QueueAttributeDataMat4 attributeDataMat4;
    QueueAttributeDataInt attributeDataInt;
    QueueAttributeDataFloat attributeDataFloat;
    QueueAttributeDataBool attributeDataBool;
    QueueAttributeDataEnum attributeDataEnum;
    QueueTextureSampler textureSampler;
    QueueTextureData textureData;
  };
};

#pragma pack(pop)
static constexpr size_t QueueDataSize[] = {
    sizeof(QueueHeader) + sizeof(QueueOperateBegin),
    sizeof(QueueHeader) + sizeof(QueueOperateEnd),
    sizeof(QueueHeader) + sizeof(QueueFrameMark),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataUInt32),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataFloat4),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataMat4),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataInt),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataUInt32),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataFloat),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataBool),
    sizeof(QueueHeader) + sizeof(QueueAttributeDataEnum),
    sizeof(QueueHeader) + sizeof(QueueTextureSampler),
    sizeof(QueueHeader) + sizeof(QueueTextureData),
    sizeof(QueueHeader),
    sizeof(QueueHeader) + sizeof(QueueStringTransfer),
    sizeof(QueueHeader) + sizeof(QueueStringTransfer),
    sizeof(QueueHeader) + sizeof(QueueStringTransfer),
};
}  // namespace inspector