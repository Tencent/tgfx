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
#include <optional>
#include <set>
#include <thread>
#include <vector>
#include "FrameCaptureMessage.h"
#include "FrameCaptureTexture.h"
#include "LZ4CompressionHandler.h"
#include "Protocol.h"
#include "Socket.h"
#include "concurrentqueue.h"
#include "gpu/ProgramInfo.h"
#include "gpu/RRectsVertexProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

#if defined _WIN32
#include <intrin.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach/mach_time.h>
#endif

namespace tgfx::inspect {
class FrameCapture {
 public:
  static FrameCapture& GetInstance() {
    static FrameCapture inspector;
    return inspector;
  }

  FrameCapture();

  ~FrameCapture();

  bool currentFrameShouldCaptrue();

  bool isConnected();

  static uint64_t NextTextureID();

  void queueSerialFinish(const FrameCaptureMessageItem& item);

  void sendAttributeData(const char* name, const Rect& rect);

  void sendAttributeData(const char* name, const Matrix& matrix);

  void sendAttributeData(const char* name, const std::optional<Matrix>& matrix);

  void sendAttributeData(const char* name, const PMColor& color);

  void sendAttributeData(const char* name, const std::optional<PMColor>& color);

  void sendFrameMark(const char* name);

  void sendAttributeData(const char* name, int val);

  void sendAttributeData(const char* name, float val);

  void sendAttributeData(const char* name, bool val);

  void sendAttributeData(const char* name, uint8_t val, uint8_t type);

  void sendAttributeData(const char* name, uint32_t val,
                         FrameCaptureMessageType type = FrameCaptureMessageType::ValueDataUint32);

  void sendAttributeData(const char* name, float* val, int size);

  void sendInputTextureID(uint64_t textureId);

  void sendOutputTextureID(uint64_t textureId);

  void sendFragmentProcessor(Context* context,
                             const std::vector<PlacementPtr<FragmentProcessor>>& colors,
                             const std::vector<PlacementPtr<FragmentProcessor>>& coverages);

  void sendFrameCaptureTexture(std::shared_ptr<FrameCaptureTexture> frameCaptureTexture);

  void sendProgramKey(const BytesKey& programKey);

  void sendUniformValue(const std::string& name, const void* data, size_t size);

  void sendOpPtr(DrawOp* drawOp);

  void sendRectMeshData(DrawOp* drawOp, RectsVertexProvider* provider);

  void sendRRectMeshData(DrawOp* drawOp, RRectsVertexProvider* provider);

  void sendShapeMeshData(DrawOp* drawOp, std::shared_ptr<Shape> styledShape, AAType aaType,
                         const Rect& clipBounds);

  void sendMeshData(VertexProvider* provider, uint64_t extraDataPtr, size_t extraDataSize);

  void captureProgramInfo(const BytesKey& programKey, Context* context,
                          const ProgramInfo* programInfo);

  void captureRenderTarget(const RenderTarget* renderTarget);

 protected:
  enum class DequeueStatus { DataDequeued, ConnectionLost, QueueEmpty };

  enum class ThreadCtxStatus { Same, Changed };

  static void LaunchWorker(FrameCapture* inspector);

  static void LaunchEncodeWorker(FrameCapture* inspector) {
    inspector->encodeWorker();
  }

  bool shouldExit();

  void clear();

  void sendTextureID(uint64_t texturePtr, FrameCaptureMessageType type);

  void worker();

  void encodeWorker();

  void spawnWorkerThreads();

  bool handleServerQuery();

  void handleConnect(const WelcomeMessage& welcome);

  void appendDataUnsafe(const void* data, size_t len);

  bool appendData(const void* data, size_t len);

  bool needDataSize(size_t len);

  bool commitData();

  bool sendData(const uint8_t* data, size_t len);

  void sendString(uint64_t stringPtr, const char* str, FrameCaptureMessageType type);

  void sendString(uint64_t stringPtr, const char* str, size_t len, FrameCaptureMessageType type);

  void sendPixelsData(uint64_t pixelsPtr, const char* pixels, size_t len,
                      FrameCaptureMessageType type);

  void sendStringWithExtraData(uint64_t str, const char* ptr, size_t len,
                               std::shared_ptr<Data> extraData, FrameCaptureMessageType type);

  void sendLongStringWithExtraData(uint64_t str, const char* ptr, size_t len,
                                   std::shared_ptr<Data> extraData, FrameCaptureMessageType type);

  void sendShaderText(const ShaderModuleDescriptor& shaderDescriptor);

  void sendUniformInfo(const std::vector<Uniform>& uniforms);

  bool confirmProtocol();

  FrameCaptureMessageItem copyDataToDirectlySendDataMessage(const void* src, size_t size);

  DequeueStatus dequeueSerial();

 private:
  int64_t epoch = 0;
  int64_t initTime = 0;
  Buffer dataBuffer = {};
  Buffer lz4Buf = {};
  std::unique_ptr<LZ4CompressionHandler> lz4Handler = nullptr;
  std::atomic<bool> shutdown = false;
  std::atomic<int64_t> timeBegin = 0;
  std::atomic<uint64_t> frameCount = 0;
  std::atomic<bool> isConnect = false;
  std::shared_ptr<Socket> sock = nullptr;
  int64_t refTimeThread = 0;
  moodycamel::ConcurrentQueue<FrameCaptureMessageItem> serialConcurrentQueue;
  moodycamel::ConcurrentQueue<std::shared_ptr<FrameCaptureTexture>> imageQueue;
  std::unique_ptr<std::thread> messageThread = nullptr;
  std::unique_ptr<std::thread> decodeThread = nullptr;
  std::vector<std::shared_ptr<UDPBroadcast>> broadcast = {};
  const char* programName = nullptr;
  std::mutex programNameLock = {};
  int dataBufferOffset = 0;
  int dataBufferStart = 0;
  uint32_t captureFrameCount = 0;
  std::atomic<bool> _currentFrameShouldCaptrue = false;
  std::set<BytesKey> programKeys = {};
};
}  // namespace tgfx::inspect
