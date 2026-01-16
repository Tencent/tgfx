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
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx::inspect {
enum class FrameCaptureMessageType : uint8_t {
  OperateBegin,
  OperateEnd,
  OperatePtr,
  FrameMarkMessage,
  ValueDataUint32,
  ValueDataFloat4,
  ValueDataMat3,
  ValueDataInt,
  ValueDataColor,
  ValueDataFloat,
  ValueDataBool,
  ValueDataEnum,
  ProgramKey,
  ShaderText,
  UniformInfo,
  UniformValue,
  Mesh,
  InputTexture,
  OutputTexture,
  TextureData,
  KeepAlive,
  StringData,
  ValueName,
  PixelsData,
  ProgramKeyData,
  VertexShaderTextData,
  FragmentShaderTextData,
  UniformInfoData,
  UniformValueData,
  MeshData
};

#pragma pack(push, 1)
struct FrameCaptureMessageHeader {
  union {
    FrameCaptureMessageType type;
    uint8_t idx;
  };
};

struct OperateBaseMessage {
  int64_t usTime;
};

struct OperateBeginMessage : OperateBaseMessage {
  uint8_t type;
};

struct OperateEndMessage : OperateBaseMessage {
  uint8_t type;
};

struct FrameMarkMessage {
  bool captured;
  int64_t usTime;
};

struct AttributeDataMessage {
  uint64_t name;
};

struct AttributeDataUInt32Message : AttributeDataMessage {
  uint32_t value;
};

struct AttributeDataFloat4Message : AttributeDataMessage {
  float value[4];
};

struct AttributeDataMat4Message : AttributeDataMessage {
  float value[6];
};

struct AttributeDataIntMessage : AttributeDataMessage {
  int value;
};

struct AttributeDataFloatMessage : AttributeDataMessage {
  float value;
};

struct AttributeDataBoolMessage : AttributeDataMessage {
  bool value;
};

struct AttributeDataEnumMessage : AttributeDataMessage {
  uint16_t value;
};

struct StringTransferMessage {
  uint64_t ptr;
};

struct TextureSamplerMessage {
  uint64_t textureId;
};

struct TextureDataMessage : TextureSamplerMessage {
  bool isInput;
  PixelFormat format;
  int width;
  int height;
  size_t rowBytes;
  size_t pixelsSize;
  uint64_t pixels;
};

struct DirectlySendDataMessage {
  uint64_t dataPtr;
  size_t size;
};

struct ShaderTextMessage : DirectlySendDataMessage {
  uint8_t type;
};

struct UniformInfoMessage : DirectlySendDataMessage {
  uint8_t format;
};

struct UniformValueMessage : DirectlySendDataMessage {
  uint64_t valuePtr;
  size_t valueSize;
};

struct DrawOpPtrMessage {
  uint64_t drawOpPtr;
};

struct MeshMessage : DirectlySendDataMessage {
  uint64_t extraDataPtr;
  size_t extraDataSize;
};

struct FrameCaptureMessageItem {
  FrameCaptureMessageHeader hdr;
  union {
    FrameMarkMessage frameMark;
    OperateBeginMessage operateBegin;
    OperateEndMessage operateEnd;
    StringTransferMessage stringTransfer;
    AttributeDataUInt32Message attributeDataUint32;
    AttributeDataFloat4Message attributeDataFloat4;
    AttributeDataMat4Message attributeDataMat4;
    AttributeDataIntMessage attributeDataInt;
    AttributeDataFloatMessage attributeDataFloat;
    AttributeDataBoolMessage attributeDataBool;
    AttributeDataEnumMessage attributeDataEnum;
    TextureSamplerMessage textureSampler;
    TextureDataMessage textureData;
    DirectlySendDataMessage directlySendDataMessage;
    ShaderTextMessage shaderTextMessage;
    UniformInfoMessage uniformInfoMessage;
    UniformValueMessage uniformValueMessage;
    DrawOpPtrMessage drawOpPtrMessage;
    MeshMessage meshMessage;
  };
};
#pragma pack(pop)

static constexpr size_t FrameCaptureMessageDataSize[] = {
    sizeof(FrameCaptureMessageHeader) + sizeof(OperateBeginMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(OperateEndMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(DrawOpPtrMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(FrameMarkMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataUInt32Message),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataFloat4Message),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataMat4Message),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataIntMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataUInt32Message),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataFloatMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataBoolMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(AttributeDataEnumMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(DirectlySendDataMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(ShaderTextMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(UniformInfoMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(UniformValueMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(MeshMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(TextureSamplerMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(TextureSamplerMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(TextureDataMessage),
    sizeof(FrameCaptureMessageHeader),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(TextureSamplerMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
    sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage),
};
}  // namespace tgfx::inspect
