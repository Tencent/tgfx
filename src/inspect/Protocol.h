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

#include <unordered_map>

namespace tgfx::inspect {

static constexpr int LZ4HeaderSize = 12;
static constexpr int MinLZ4EncodeSize = 1024 * 4;
static constexpr int TargetFrameSize = 256 * 1024;
static constexpr int HandshakeShibbolethSize = 4;
static constexpr char HandshakeShibboleth[HandshakeShibbolethSize] = {'T', 'G', 'F', 'X'};

static constexpr size_t BroadcastCount = 5;
static constexpr size_t BroadcastPort = 8086;
static constexpr int64_t BroadcastHeartbeatUSTime = 3000000;
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
  CaptureFrame,
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
  AtlasTextOp,
  Quads3DDrawOp,
  DstTextureCopyOp,
  ResolveOp,
  OpTaskTypeSize,
};

static std::unordered_map<uint8_t, OpTaskType> DrawOpTypeToOpTaskType = {
    {0, OpTaskType::RectDrawOp},  {1, OpTaskType::RRectDrawOp},   {2, OpTaskType::ShapeDrawOp},
    {3, OpTaskType::AtlasTextOp}, {4, OpTaskType::Quads3DDrawOp},
};

enum class CustomEnumType : uint8_t {
  BufferType = 0,
  BlendMode,
  AAType,
  PixelFormat,
  ImageOrigin,
};

enum class LayerTreeMessage : uint8_t {
  EnableLayerInspector,
  HoverLayerAddress,
  SelectedLayerAddress,
  SerializeAttribute,
  SerializeSubAttribute,
  FlushAttribute,
  FlushLayerTree,
  FlushImage,
  PickedLayerAddress,
  FlushAttributeAck,
  LayerTree,
  LayerAttribute,
  LayerSubAttribute,
  ImageData
};

enum class VertexProviderType { RectsVertexProvider, RRectsVertexProvider };

struct MeshInfo {
  size_t rectCount = 0;
  uint64_t drawOpPtr = 0;
};

struct RectMeshInfo : MeshInfo {
  uint8_t aaType = 2;
  bool hasUVCoord = false;
  bool hasColor = false;
  bool hasSubset = false;
};

struct RRectMeshInfo : MeshInfo {
  bool hasColor = false;
  bool hasStroke = false;
};
}  // namespace tgfx::inspect
