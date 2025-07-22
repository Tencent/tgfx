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
#include <thread>
#include <vector>
#include "MemoryUtils.h"
#include "Protocol.h"
#include "Queue.h"
#include "Singleton.h"
#include "Socket.h"
#include "concurrentqueue.h"

#if defined _WIN32
#include <intrin.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach/mach_time.h>
#endif

namespace inspector {
#define QueuePrepare(_type) \
  auto item = QueueItem();  \
  MemWrite(&item.hdr.type, _type);
#define QueueCommit() Inspector::QueueSerialFinish(item);

class Inspector;
typedef Singleton<Inspector> InspectorSingleton;

class Inspector {
 public:
  Inspector();
  ~Inspector();

  static int64_t GetTime() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
  }

  static void QueueSerialFinish(const QueueItem& item) {
    auto inspector = InspectorSingleton::GetInstance();
    inspector->serialConcurrentQueue.enqueue(item);
  }

  static void SendFrameMark(const char* name) {
    if (!name) {
      InspectorSingleton::GetInstance()->frameCount.fetch_add(1, std::memory_order_relaxed);
    }
    auto item = QueueItem();
    MemWrite(&item.hdr.type, QueueType::FrameMarkMsg);
    MemWrite(&item.frameMark.nsTime, GetTime());
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, int val) {
    QueuePrepare(QueueType::ValueDataInt);
    MemWrite(&item.attributeDataInt.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataInt.value, val);
    QueueCommit();
  }

  static void SendAttributeData(const char* name, float val) {
    QueuePrepare(QueueType::ValueDataFloat)
        MemWrite(&item.attributeDataFloat.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataFloat.value, val);
    QueueCommit();
  }

  static void SendAttributeData(const char* name, bool val) {
    QueuePrepare(QueueType::ValueDataBool);
    MemWrite(&item.attributeDataBool.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataBool.value, val);
    QueueCommit();
  }

  static void SendAttributeData(const char* name, uint8_t val, uint8_t type) {
    QueuePrepare(QueueType::ValueDataEnum);
    MemWrite(&item.attributeDataEnum.name, reinterpret_cast<uint64_t>(name));
    auto value = static_cast<uint16_t>(type << 8 | val);
    MemWrite(&item.attributeDataEnum.value, value);
    QueueCommit();
  }

  static void SendAttributeData(const char* name, uint32_t val,
                                QueueType type = QueueType::ValueDataUint32) {
    QueuePrepare(type);
    MemWrite(&item.attributeDataUint32.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataUint32.value, val);
    QueueCommit();
  }

  static void SendAttributeData(const char* name, float* val, int size) {
    if (size == 4) {
      QueuePrepare(QueueType::ValueDataFloat4);
      MemWrite(&item.attributeDataFloat4.name, reinterpret_cast<uint64_t>(name));
      MemWrite(item.attributeDataFloat4.value, val, static_cast<size_t>(size) * sizeof(float));
      QueueCommit();
    } else if (size == 6) {
      QueuePrepare(QueueType::ValueDataMat4);
      MemWrite(&item.attributeDataMat4.name, reinterpret_cast<uint64_t>(name));
      MemWrite(item.attributeDataMat4.value, val, static_cast<size_t>(size) * sizeof(float));
      QueueCommit();
    }
  }

  static void LaunchWorker(Inspector* inspector) {
    inspector->worker();
  }

  static bool ShouldExit();

  void sendString(uint64_t str, const char* ptr, QueueType type) {
    sendString(str, ptr, strlen(ptr), type);
  }
  void sendString(uint64_t str, const char* ptr, size_t len, QueueType type);

  void worker();
  void spawnWorkerThreads();
  bool handleServerQuery();

 private:
  enum class DequeueStatus { DataDequeued, ConnectionLost, QueueEmpty };
  enum class ThreadCtxStatus { Same, Changed };

  bool appendData(const void* data, size_t len) {
    const auto ret = needDataSize(len);
    appendDataUnsafe(data, len);
    return ret;
  }

  bool needDataSize(size_t len) {
    bool ret = true;
    if (size_t(dataBufferOffset - dataBufferStart) + len > TargetFrameSize) {
      ret = commitData();
    }
    return ret;
  }

  void appendDataUnsafe(const void* data, size_t len) {
    memcpy(dataBuffer + dataBufferOffset, data, len);
    dataBufferOffset += int(len);
  }

  bool commitData();
  bool sendData(const char* data, size_t len);
  DequeueStatus dequeueSerial();
  bool confirmProtocol();
  void handleConnect(WelcomeMessage& welcome);

 private:
  const uint16_t broadcastPort = 8086;
  int64_t epoch = 0;
  int64_t initTime = 0;
  char* dataBuffer = nullptr;
  char* lz4Buf = nullptr;
  void* lz4Stream = nullptr;
  std::atomic<bool> shutdown = false;
  std::atomic<int64_t> timeBegin = 0;
  std::atomic<uint64_t> frameCount = 0;
  std::atomic<bool> isConnect = false;
  std::shared_ptr<Socket> sock = nullptr;
  int64_t refTimeThread = 0;
  moodycamel::ConcurrentQueue<QueueItem> serialConcurrentQueue;
  std::mutex serialLock;

  std::unique_ptr<std::thread> messageThread = nullptr;
  std::vector<std::shared_ptr<UdpBroadcast>> broadcast;
  const char* programName = nullptr;
  std::mutex programNameLock;
  int dataBufferOffset = 0;
  int dataBufferStart = 0;
};
}  // namespace inspector