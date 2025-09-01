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
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include "FrameCaptureMessage.h"
#include "LZ4CompressionHandler.h"
#include "Protocol.h"
#include "Socket.h"
#include "concurrentqueue.h"
#include "gpu/CommandQueue.h"
#include "gpu/GPUTexture.h"
#include "gpu/ProgramInfo.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Clock.h"
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
  static FrameCapture& GetFrameCapture() {
    static FrameCapture inspector;
    return inspector;
  }

  FrameCapture();

  ~FrameCapture();

  static uint64_t NextTextureID();

  static void QueueSerialFinish(const FrameCaptureMessageItem& item);

  static void SendAttributeData(const char* name, const Rect& rect);

  static void SendAttributeData(const char* name, const Matrix& matrix);

  static void SendAttributeData(const char* name, const std::optional<Matrix>& matrix);

  static void SendAttributeData(const char* name, const Color& color);

  static void SendAttributeData(const char* name, const std::optional<Color>& color);

  static void SendFrameMark(const char* name);

  static void SendAttributeData(const char* name, int val);

  static void SendAttributeData(const char* name, float val);

  static void SendAttributeData(const char* name, bool val);

  static void SendAttributeData(const char* name, uint8_t val, uint8_t type);

  static void SendAttributeData(
      const char* name, uint32_t val,
      FrameCaptureMessageType type = FrameCaptureMessageType::ValueDataUint32);

  static void SendAttributeData(const char* name, float* val, int size);

  static void SendOpInputTexture(uint64_t texturePtr);

  static void SendOpOutputTexture(const GPUTexture* texturePtr);

  static void SendOpOutputTexture(uint64_t texturePtr);

  static void SendInputTextureData(uint64_t texturePtr, int width, int height, size_t rowBytes,
                                   uint8_t format, const void* pixels);

  static void SendInputTextureData(uint64_t texturePtr, int width, int height, size_t rowBytes,
                                   uint8_t format, std::shared_ptr<Buffer> imageBuffer);

  static void SendInputTextureData(const GPUTexture* texturePtr, int width, int height,
                                   size_t rowBytes, PixelFormat format, const void* pixels);

  static void SendFragmentProcessor(
      const std::vector<PlacementPtr<FragmentProcessor>>& programInfo);

  static void SendInputTextureData(const CommandQueue* commandQueue, GPUTexture* texturePtr);

  static void SendOutputTextureData(const RenderTarget* renderTarget);

 protected:
  struct ImageItem {
    bool isInput = false;
    uint8_t format = 0;
    int width = 0;
    int height = 0;
    size_t rowBytes = 0;
    uint64_t texturePtr = 0;
    std::shared_ptr<Buffer> image = nullptr;
  };

  enum class DequeueStatus { DataDequeued, ConnectionLost, QueueEmpty };

  enum class ThreadCtxStatus { Same, Changed };

  static void LaunchWorker(FrameCapture* inspector);

  static void LaunchEncodeWorker(FrameCapture* inspector) {
    inspector->encodeWorker();
  }

  static bool ShouldExit();

  static bool CurrentFrameShouldCaptrue();

  static void SendOpTexture(uint64_t texturePtr, FrameCaptureMessageType type);

  static uint64_t GetTextureHash(uint64_t texturePtr, uint64_t currentFrame = 0);

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

  void sendString(uint64_t str, const char* ptr, FrameCaptureMessageType type);

  void sendString(uint64_t str, const char* ptr, size_t len, FrameCaptureMessageType type);

  void sendPixelsData(uint64_t str, const char* ptr, size_t len, FrameCaptureMessageType type);

  bool confirmProtocol();

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
  moodycamel::ConcurrentQueue<ImageItem> imageQueue;
  std::unique_ptr<std::thread> messageThread = nullptr;
  std::unique_ptr<std::thread> decodeThread = nullptr;
  std::vector<std::shared_ptr<UDPBroadcast>> broadcast = {};
  const char* programName = nullptr;
  std::mutex programNameLock;
  int dataBufferOffset = 0;
  int dataBufferStart = 0;
  uint32_t captureFrameCount = 0;
  std::atomic<bool> currentFrameShouldCaptrue = false;
  std::unordered_map<uint64_t, uint64_t> textureIds = {};
};
}  // namespace tgfx::inspect