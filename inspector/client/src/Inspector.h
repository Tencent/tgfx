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
#include "Message.h"
#include "Protocol.h"
#include "Singleton.h"
#include "Socket.h"
#include "TimeUtils.h"
#include "concurrentqueue.h"

#if defined _WIN32
#include <intrin.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach/mach_time.h>
#endif

namespace inspector {
#define MsgPrepare(_type) \
  auto item = MsgItem();  \
  MemWrite(&item.hdr.type, _type);
#define MsgCommit() Inspector::QueueSerialFinish(item);

class Inspector;
typedef Singleton<Inspector> InspectorSingleton;

class Inspector {
  struct ImageItem
  {
    uint8_t format = 0;
    int width = 0;
    int height = 0;
    size_t rowBytes = 0;
    uint64_t samplerPtr = 0;
    std::shared_ptr<uint8_t> image = nullptr;
  };

 public:
  Inspector();
  ~Inspector();

  static void QueueSerialFinish(const MsgItem& item) {
    auto inspector = InspectorSingleton::GetInstance();
    inspector->serialConcurrentQueue.enqueue(item);
  }

  static void SendFrameMark(const char* name) {
    if (!name) {
      InspectorSingleton::GetInstance()->frameCount.fetch_add(1, std::memory_order_relaxed);
    }
    auto item = MsgItem();
    MemWrite(&item.hdr.type, MsgType::FrameMarkMsg);
    MemWrite(&item.frameMark.nsTime, GetCurrentTime<std::chrono::nanoseconds>());
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, int val) {
    MsgPrepare(MsgType::ValueDataInt);
    MemWrite(&item.attributeDataInt.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataInt.value, val);
    MsgCommit();
  }

  static void SendAttributeData(const char* name, float val) {
    MsgPrepare(MsgType::ValueDataFloat);
    MemWrite(&item.attributeDataFloat.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataFloat.value, val);
    MsgCommit();
  }

  static void SendAttributeData(const char* name, bool val) {
    MsgPrepare(MsgType::ValueDataBool);
    MemWrite(&item.attributeDataBool.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataBool.value, val);
    MsgCommit();
  }

  static void SendAttributeData(const char* name, uint8_t val, uint8_t type) {
    MsgPrepare(MsgType::ValueDataEnum);
    MemWrite(&item.attributeDataEnum.name, reinterpret_cast<uint64_t>(name));
    auto value = static_cast<uint16_t>(type << 8 | val);
    MemWrite(&item.attributeDataEnum.value, value);
    MsgCommit();
  }

  static void SendAttributeData(const char* name, uint32_t val,
                                MsgType type = MsgType::ValueDataUint32) {
    MsgPrepare(type);
    MemWrite(&item.attributeDataUint32.name, reinterpret_cast<uint64_t>(name));
    MemWrite(&item.attributeDataUint32.value, val);
    MsgCommit();
  }

  static void SendAttributeData(const char* name, float* val, int size) {
    if (size == 4) {
      MsgPrepare(MsgType::ValueDataFloat4);
      MemWrite(&item.attributeDataFloat4.name, reinterpret_cast<uint64_t>(name));
      MemWrite(item.attributeDataFloat4.value, val, static_cast<size_t>(size) * sizeof(float));
      MsgCommit();
    } else if (size == 6) {
      MsgPrepare(MsgType::ValueDataMat4);
      MemWrite(&item.attributeDataMat4.name, reinterpret_cast<uint64_t>(name));
      MemWrite(item.attributeDataMat4.value, val, static_cast<size_t>(size) * sizeof(float));
      MsgCommit();
    }
  }

  static void SendOpTextureSampler(uint64_t samplerPtr) {
    MsgPrepare(MsgType::TextureSampler);
    MemWrite(&item.textureSampler.samplerPtr, samplerPtr);
    MsgCommit();
  }

  static void SendTextureData(uint64_t samplerPtr, int width, int height, size_t rowBytes,
                              uint8_t format, const void* pixels) {
    auto inspector = InspectorSingleton::GetInstance();
    const auto sz = static_cast<size_t>(width) * rowBytes;
    std::shared_ptr<uint8_t> ptr(new uint8_t(sz));
    MemWrite(ptr.get(), pixels, sz);

    auto imageItem = ImageItem();
    imageItem.image = std::move(ptr);
    imageItem.samplerPtr = samplerPtr;
    imageItem.width = width;
    imageItem.height = height;
    imageItem.format = format;
    imageItem.rowBytes = rowBytes;
    inspector->imageQueue.enqueue(imageItem);
  }

  static void LaunchWorker(Inspector* inspector) {
    inspector->worker();
  }

  static void LaunchCompressWorker(Inspector* inspector) {
    inspector->compressWorker();
  }

  static bool ShouldExit();

  void sendString(uint64_t str, const char* ptr, MsgType type) {
    sendString(str, ptr, strlen(ptr), type);
  }
  void sendString(uint64_t str, const char* ptr, size_t len, MsgType type);
  void sendLongString(uint64_t str, const char* ptr, size_t len, MsgType type);

  void worker();
  void compressWorker();
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
  moodycamel::ConcurrentQueue<MsgItem> serialConcurrentQueue;
  moodycamel::ConcurrentQueue<ImageItem> imageQueue;

  std::unique_ptr<std::thread> messageThread = nullptr;
  std::unique_ptr<std::thread> compressThread = nullptr;
  std::vector<std::shared_ptr<UdpBroadcast>> broadcast;
  const char* programName = nullptr;
  std::mutex programNameLock;
  int dataBufferOffset = 0;
  int dataBufferStart = 0;
};
}  // namespace inspector