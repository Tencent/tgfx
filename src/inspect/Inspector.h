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
#include "InspectorMessage.h"
#include "Protocol.h"
#include "Socket.h"
#include "concurrentqueue.h"
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
class Inspector {
 public:
  static Inspector& GetInspector() {
    static Inspector inspector;
    return inspector;
  }

  Inspector();

  ~Inspector();

  static void QueueSerialFinish(const MsgItem& item);

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

  static void SendAttributeData(const char* name, uint32_t val,
                                MsgType type = MsgType::ValueDataUint32);

  static void SendAttributeData(const char* name, float* val, int size);

 protected:
  enum class DequeueStatus { DataDequeued, ConnectionLost, QueueEmpty };

  enum class ThreadCtxStatus { Same, Changed };

  static void LaunchWorker(Inspector* inspector);

  static bool ShouldExit();

  void worker();

  void spawnWorkerThreads();

  bool handleServerQuery();

  void handleConnect(const WelcomeMessage& welcome);

  void appendDataUnsafe(const void* data, size_t len);

  bool appendData(const void* data, size_t len);

  bool needDataSize(size_t len);

  bool commitData();

  bool sendData(const char* data, size_t len);

  void sendString(uint64_t str, const char* ptr, MsgType type);

  void sendString(uint64_t str, const char* ptr, size_t len, MsgType type);

  bool confirmProtocol();

  DequeueStatus dequeueSerial();

 private:
  const uint16_t broadcastPort = 8086;
  int64_t epoch = 0;
  int64_t initTime = 0;
  Buffer dataBuffer = {};
  Buffer lz4Buf = {};
  void* lz4Stream = nullptr;
  std::atomic<bool> shutdown = false;
  std::atomic<int64_t> timeBegin = 0;
  std::atomic<uint64_t> frameCount = 0;
  std::atomic<bool> isConnect = false;
  std::shared_ptr<Socket> sock = nullptr;
  int64_t refTimeThread = 0;
  moodycamel::ConcurrentQueue<MsgItem> serialConcurrentQueue;
  std::unique_ptr<std::thread> messageThread = nullptr;
  std::vector<std::shared_ptr<UdpBroadcast>> broadcast = {};
  const char* programName = nullptr;
  std::mutex programNameLock;
  int dataBufferOffset = 0;
  int dataBufferStart = 0;
};
}  // namespace tgfx::inspect