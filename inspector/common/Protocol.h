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
namespace inspector {

inline constexpr int broadcastNum = 5;

constexpr unsigned Lz4CompressBound(unsigned isize) {
  return isize + (isize / 255) + 16;
}
using lz4sz_t = int;

const int MaxTargetSize = 1024 * 1024 * 100;
enum { TargetFrameSize = 256 * 1024 };
enum { LZ4Size = Lz4CompressBound(TargetFrameSize) };
enum { HandshakeShibbolethSize = 4 };
static const char HandshakeShibboleth[HandshakeShibbolethSize] = {'T', 'G', 'F', 'X'};

enum { WelcomeMessageProgramNameSize = 64 };
enum { WelcomeMessageHostInfoSize = 1024 };
enum : uint8_t { ProtocolVersion = 1 };
enum : uint16_t { BroadcastVersion = 3 };

enum HandshakeStatus : uint8_t {
  HandshakePending,
  HandshakeWelcome,
  HandshakeProtocolMismatch,
  HandshakeNotAvailable,
  HandshakeDropped
};

enum MsgType : uint8_t { FrameCapture = 0, LayerTree = 1 };

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

enum ServerQuery : uint8_t {
  ServerQueryTerminate,
  ServerQueryString,
  ServerQueryValueName,
  ServerQueryDisconnect,
};

struct ServerQueryPacket {
  ServerQuery type;
  uint64_t ptr;
  uint32_t extra;
};

enum { ServerQueryPacketSize = sizeof(ServerQueryPacket) };

enum OpTaskType : uint8_t {
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

enum TGFXEnum : uint8_t {
  BufferType = 0,
  BlendMode,
  AAType,
  PixelFormat,
  ImageOrigin,
};
}  // namespace inspector