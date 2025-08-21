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
#include "tgfx/core/Buffer.h"

namespace tgfx::debug {
enum class MsgType : uint8_t {
  OperateBegin,
  OperateEnd,
  FrameMarkMsg,
  ValueDataUint32,
  ValueDataFloat4,
  ValueDataMat3,
  ValueDataInt,
  ValueDataColor,
  ValueDataFloat,
  ValueDataBool,
  ValueDataEnum,
  Texture,
  TextureData,
  KeepAlive,
  StringData,
  ValueName,
  PixelsData,
};

#pragma pack(push, 1)
struct MsgHeader {
  union {
    MsgType type;
    uint8_t idx;
  };
};

struct OperateBaseMsg {
  int64_t usTime;
};

struct OperateBeginMsg : OperateBaseMsg {
  uint8_t type;
};

struct OperateEndMsg : OperateBaseMsg {
  uint8_t type;
};

struct FrameMarkMsg {
  int64_t usTime;
};

struct AttributeDataMsg {
  uint64_t name;
};

struct AttributeDataUInt32Msg : AttributeDataMsg {
  uint32_t value;
};

struct AttributeDataFloat4Msg : AttributeDataMsg {
  float value[4];
};

struct AttributeDataMat4Msg : AttributeDataMsg {
  float value[6];
};

struct AttributeDataIntMsg : AttributeDataMsg {
  int value;
};

struct AttributeDataFloatMsg : AttributeDataMsg {
  float value;
};

struct AttributeDataBoolMsg : AttributeDataMsg {
  bool value;
};

struct AttributeDataEnumMsg : AttributeDataMsg {
  uint16_t value;
};

struct StringTransferMsg {
  uint64_t ptr;
};

struct TextureSamplerMsg {
  uint64_t texturePtr;
};

struct TextureDataMsg : TextureSamplerMsg {
  uint8_t format;
  int width;
  int height;
  size_t rowBytes;
  uint64_t pixels;
};

struct MsgItem {
  MsgHeader hdr;
  union {
    FrameMarkMsg frameMark;
    OperateBeginMsg operateBegin;
    OperateEndMsg operateEnd;
    StringTransferMsg stringTransfer;
    AttributeDataUInt32Msg attributeDataUint32;
    AttributeDataFloat4Msg attributeDataFloat4;
    AttributeDataMat4Msg attributeDataMat4;
    AttributeDataIntMsg attributeDataInt;
    AttributeDataFloatMsg attributeDataFloat;
    AttributeDataBoolMsg attributeDataBool;
    AttributeDataEnumMsg attributeDataEnum;
    TextureSamplerMsg textureSampler;
    TextureDataMsg textureData;
  };
};
#pragma pack(pop)

static constexpr size_t MsgDataSize[] = {
    sizeof(MsgHeader) + sizeof(OperateBeginMsg),
    sizeof(MsgHeader) + sizeof(OperateEndMsg),
    sizeof(MsgHeader) + sizeof(FrameMarkMsg),
    sizeof(MsgHeader) + sizeof(AttributeDataUInt32Msg),
    sizeof(MsgHeader) + sizeof(AttributeDataFloat4Msg),
    sizeof(MsgHeader) + sizeof(AttributeDataMat4Msg),
    sizeof(MsgHeader) + sizeof(AttributeDataIntMsg),
    sizeof(MsgHeader) + sizeof(AttributeDataUInt32Msg),
    sizeof(MsgHeader) + sizeof(AttributeDataFloatMsg),
    sizeof(MsgHeader) + sizeof(AttributeDataBoolMsg),
    sizeof(MsgHeader) + sizeof(AttributeDataEnumMsg),
    sizeof(MsgHeader) + sizeof(TextureSamplerMsg),
    sizeof(MsgHeader) + sizeof(TextureDataMsg),
    sizeof(MsgHeader),
    sizeof(MsgHeader) + sizeof(StringTransferMsg),
    sizeof(MsgHeader) + sizeof(StringTransferMsg),
    sizeof(MsgHeader) + sizeof(TextureSamplerMsg),
};
}  // namespace tgfx::debug