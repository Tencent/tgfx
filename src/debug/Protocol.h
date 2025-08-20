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
namespace tgfx::debug {

using lz4sz_t = int;
static constexpr int Lz4CompressBound(int size) {
  return size + (size / 255) + 16;
}
static constexpr int TargetFrameSize = 256 * 1024;
static constexpr int LZ4Size = Lz4CompressBound(TargetFrameSize);
static constexpr int HandshakeShibbolethSize = 4;
static constexpr char HandshakeShibboleth[HandshakeShibbolethSize] = {'T', 'G', 'F', 'X'};

static constexpr size_t BroadcastNum = 5;
static constexpr int WelcomeMessageProgramNameSize = 64;
static constexpr int WelcomeMessageHostInfoSize = 1024;
static constexpr uint8_t ProtocolVersion = 1;
static constexpr uint16_t BroadcastVersion = 1;

enum class HandshakeStatus : uint8_t {
  HandshakePending,
  HandshakeWelcome,
  HandshakeProtocolMismatch,
  HandshakeNotAvailable,
  HandshakeDropped
};

enum class ToolType : uint8_t { FrameCapture = 0, LayerTree = 1 };

struct BroadcastMessage {
  uint8_t type;
  uint16_t listenPort;
  uint32_t protocolVersion;
  uint64_t pid;
  int32_t activeTime;  // in seconds
  char programName[WelcomeMessageProgramNameSize];
};

struct WelcomeMessage {
  int64_t initBegin;
  int64_t initEnd;
  int64_t refTime;
};

enum class ServerQuery : uint8_t {
  Terminate,
  String,
  ValueName,
  Disconnect,
};

struct ServerQueryPacket {
  ServerQuery type;
  uint64_t ptr;
  uint32_t extra;
};

enum class OpTaskType : uint8_t {
  Unknown = 0,
  Flush,
  ResourceTask,
  TextureUploadTask,
  ShapeBufferUploadTask,
  GpuUploadTask,
  TextureCreateTask,
  RenderTargetCreateTask,
  TextureFlattenTask,
  RenderTask,
  RenderTargetCopyTask,
  RuntimeDrawTask,
  TextureResolveTask,
  OpsRenderTask,
  ClearOp,
  RectDrawOp,
  RRectDrawOp,
  ShapeDrawOp,
  DstTextureCopyOp,
  ResolveOp,
  OpTaskTypeSize,
};

enum class CustomEnumType : uint8_t {
  BufferType = 0,
  BlendMode,
  AAType,
  PixelFormat,
  ImageOrigin,
};
}  // namespace tgfx::debug