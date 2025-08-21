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
#include "Message.h"
#include "Protocol.h"
#include "Socket.h"
#include "concurrentqueue.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/Pipeline.h"
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

namespace tgfx::debug {
class Inspector {
  struct ImageItem
  {
    uint8_t format = 0;
    int width = 0;
    int height = 0;
    size_t rowBytes = 0;
    uint64_t texturePtr = 0;
    std::shared_ptr<Buffer> image = nullptr;
  };

 public:
  static Inspector& GetInspector() {
    static Inspector inspector;
    return inspector;
  }

  Inspector();

  ~Inspector();

  static void QueueSerialFinish(const MsgItem& item) {
    GetInspector().serialConcurrentQueue.enqueue(item);
  }

  static void SendOpTexture(uint64_t texturePtr) {
    auto item = MsgItem();
    item.hdr.type = MsgType::Texture;
    item.textureSampler.texturePtr = texturePtr;
    QueueSerialFinish(item);
  }

  static void SendTextureData(uint64_t texturePtr, int width, int height, size_t rowBytes,
                              uint8_t format, const void* pixels) {
    const auto sz = static_cast<size_t>(width) * rowBytes;
    // std::shared_ptr<uint8_t> ptr(new uint8_t[sz]);
    auto imageBuffer = std::make_shared<Buffer>(sz);
    imageBuffer->writeRange(0, sz, pixels);
    // memcpy(imageBuffer-, pixels, sz);

    auto imageItem = ImageItem();
    imageItem.image = std::move(imageBuffer);
    imageItem.texturePtr = texturePtr;
    imageItem.width = width;
    imageItem.height = height;
    imageItem.format = format;
    imageItem.rowBytes = rowBytes;
    GetInspector().imageQueue.enqueue(imageItem);
  }

  static void SendPipelineData(const PlacementPtr<Pipeline>& pipeline) {
    for (size_t i = 0; i < pipeline->numFragmentProcessors(); ++i) {
      auto processor = pipeline->getFragmentProcessor(i);
      FragmentProcessor::Iter fpIter(processor);
      while (const auto* subFP = fpIter.next()) {
        for (size_t j = 0; j < subFP->numTextureSamplers(); ++j) {
          auto texture = subFP->textureAt(j);
          SendOpTexture(reinterpret_cast<uint64_t>(texture));
        }
      }
    }
  }

  static void SendTextureData(GPUTexture* samplerPtr, int width, int height, size_t rowBytes,
                              PixelFormat format, const void* pixels) {
    SendTextureData(reinterpret_cast<uint64_t>(samplerPtr), width, height, rowBytes,
                    static_cast<uint8_t>(format), pixels);
  }

  static void SendAttributeData(const char* name, const Rect& rect) {
    float value[4] = {rect.left, rect.right, rect.top, rect.bottom};
    SendAttributeData(name, value, 4);
  }

  static void SendAttributeData(const char* name, const Matrix& matrix) {
    float value[6] = {1, 0, 0, 0, 1, 0};
    value[0] = matrix.getScaleX();
    value[1] = matrix.getSkewX();
    value[2] = matrix.getTranslateX();
    value[3] = matrix.getSkewY();
    value[4] = matrix.getScaleY();
    value[5] = matrix.getTranslateY();
    SendAttributeData(name, value, 6);
  }

  static void SendAttributeData(const char* name, const std::optional<Matrix>& matrix) {
    auto value = Matrix::MakeAll(1, 0, 0, 0, 1, 0);
    if (matrix.has_value()) {
      value = matrix.value();
    }
    SendAttributeData(name, value);
  }

  static void SendAttributeData(const char* name, const Color& color) {
    auto r = static_cast<uint8_t>(color.red * 255.f);
    auto g = static_cast<uint8_t>(color.green * 255.f);
    auto b = static_cast<uint8_t>(color.blue * 255.f);
    auto a = static_cast<uint8_t>(color.alpha * 255.f);
    auto value = static_cast<uint32_t>(r | g << 8 | b << 16 | a << 24);
    SendAttributeData(name, value, MsgType::ValueDataColor);
  }

  static void SendAttributeData(const char* name, const std::optional<Color>& color) {
    auto value = Color::FromRGBA(255, 255, 255, 255);
    if (color.has_value()) {
      value = color.value();
    }
    SendAttributeData(name, value);
  }

  static void SendFrameMark(const char* name) {
    if (!name) {
      GetInspector().frameCount.fetch_add(1, std::memory_order_relaxed);
    }
    auto item = MsgItem();
    item.hdr.type = MsgType::FrameMarkMsg;
    item.frameMark.usTime = Clock::Now();
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, int val) {
    auto item = MsgItem();
    item.hdr.type = MsgType::ValueDataInt;
    item.attributeDataInt.name = reinterpret_cast<uint64_t>(name);
    item.attributeDataInt.value = val;
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, float val) {
    auto item = MsgItem();
    item.hdr.type = MsgType::ValueDataFloat;
    item.attributeDataFloat.name = reinterpret_cast<uint64_t>(name);
    item.attributeDataFloat.value = val;
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, bool val) {
    auto item = MsgItem();
    item.hdr.type = MsgType::ValueDataBool;
    item.attributeDataBool.name = reinterpret_cast<uint64_t>(name);
    item.attributeDataBool.value = val;
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, uint8_t val, uint8_t type) {
    auto item = MsgItem();
    item.hdr.type = MsgType::ValueDataEnum;
    item.attributeDataEnum.name = reinterpret_cast<uint64_t>(name);
    item.attributeDataEnum.value = static_cast<uint16_t>(type << 8 | val);
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, uint32_t val,
                                MsgType type = MsgType::ValueDataUint32) {
    auto item = MsgItem();
    item.hdr.type = type;
    item.attributeDataUint32.name = reinterpret_cast<uint64_t>(name);
    item.attributeDataUint32.value = val;
    QueueSerialFinish(item);
  }

  static void SendAttributeData(const char* name, float* val, int size) {
    if (size == 4) {
      auto item = MsgItem();
      item.hdr.type = MsgType::ValueDataFloat4;
      item.attributeDataFloat4.name = reinterpret_cast<uint64_t>(name);
      memcpy(item.attributeDataFloat4.value, val, static_cast<size_t>(size) * sizeof(float));
      QueueSerialFinish(item);
    } else if (size == 6) {
      auto item = MsgItem();
      item.hdr.type = MsgType::ValueDataMat3;
      item.attributeDataMat4.name = reinterpret_cast<uint64_t>(name);
      memcpy(item.attributeDataMat4.value, val, static_cast<size_t>(size) * sizeof(float));
      QueueSerialFinish(item);
    }
  }

 protected:
  enum class DequeueStatus { DataDequeued, ConnectionLost, QueueEmpty };

  enum class ThreadCtxStatus { Same, Changed };

  static void LaunchWorker(Inspector* inspector) {
    inspector->worker();
  }

  static void LaunchCompressWorker(Inspector* inspector) {
    inspector->compressWorker();
  }

  static bool ShouldExit();

  void worker();

  void compressWorker();

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

  void sendLongString(uint64_t str, const char* ptr, size_t len, MsgType type);

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
  moodycamel::ConcurrentQueue<ImageItem> imageQueue;
  std::unique_ptr<std::thread> messageThread = nullptr;
  std::unique_ptr<std::thread> compressThread = nullptr;
  std::vector<std::shared_ptr<UdpBroadcast>> broadcast = {};
  const char* programName = nullptr;
  std::mutex programNameLock;
  int dataBufferOffset = 0;
  int dataBufferStart = 0;
};
}  // namespace tgfx::debug