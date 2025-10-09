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
#include <QObject>
#include <cstdint>
#include <string>
#include <thread>
#include "DataContext.h"
#include "DecodeStream.h"
#include "Protocol.h"
#include "Queue.h"
#include "Socket.h"

namespace inspector {
class Worker : public QObject {
 public:
  struct NetBuffer {
    int bufferOffset;
    int size;
  };

  struct TexturePixels {
    std::shared_ptr<tgfx::Data> pixels;
  };

  explicit Worker(const std::string& filePath);
  Worker(const char* addr, uint16_t port);
  ~Worker() override;

  bool openFile(const std::string& filePath);
  bool saveFile(const std::string& filePath);

  int64_t getFrameTime(const FrameData& fd, size_t idx) const;
  int64_t getLastTime() const;
  int64_t getFrameStart(uint32_t index) const;
  int64_t getFrameDrawCall(uint32_t index) const;
  int64_t getFrameTriangles(uint32_t index) const;
  size_t getFrameCount() const;
  bool hasExpection() const;
  std::vector<std::string>& getErrorMessage();

  FrameData* getFrameData();
  const DataContext& getDataContext() const;

 private:
  DecodeStream readBodyBytes(DecodeStream* stream);

  void shutdown();
  void exec();
  void netWork();

  void newOpTask(const std::shared_ptr<OpTaskData>& opTask);
  void query(ServerQuery type, uint64_t data, uint32_t extra = 0);
  void queryTerminate();
  bool dispatchProcess(const QueueItem& ev, const char*& ptr);
  bool process(const QueueItem& ev);
  void processOperateBegin(const QueueOperateBegin& ev);
  void processOperateEnd(const QueueOperateEnd& ev);
  void processAttributeImpl(DataHead& head, std::shared_ptr<tgfx::Data> data);
  void processFloatValue(const QueueAttributeDataFloat& ev);
  void processFloat4Value(const QueueAttributeDataFloat4& ev);
  void processIntValue(const QueueAttributeDataInt& ev);
  void processBoolValue(const QueueAttributeDataBool& ev);
  void processMat4Value(const QueueAttributeDataMat4& ev);
  void processEnumValue(const QueueAttributeDataEnum& ev);
  void processUint32Value(const QueueAttributeDataUInt32& ev);
  void processColorValue(const QueueAttributeDataUInt32& ev);
  void processFrameMark(const QueueFrameMark& ev);
  void processTextureData(const QueueTextureData& ev);
  void processTextureSampler(const QueueTextureSampler& ev);

  void addPixelsData(const char* data, size_t sz);

  void handleValueName(uint64_t name, const char* str, size_t sz);
  void handleTexturePixels(uint64_t samplerPtr, const void* data, size_t sz);

  int64_t tscTime(int64_t tsc) const {
    return tsc - dataContext.baseTime;
  }
  int64_t tscTime(uint64_t tsc) const {
    return static_cast<int64_t>(tsc) - dataContext.baseTime;
  }

 private:
  Socket sock;
  std::string addr;
  uint16_t port;

  void* lz4Stream;
  char* dataBuffer;
  int bufferOffset;

  DataContext dataContext;

  std::thread workThread;
  std::thread netThread;
  std::atomic<bool> isConnected{false};
  std::atomic<bool> isShutDown{false};
  std::atomic<bool> hasData{false};
  std::atomic<uint8_t> handshake{0};

  std::atomic<uint64_t> bytes{0};
  std::atomic<uint64_t> decBytes{0};

  std::vector<NetBuffer> netRead;
  std::mutex netReadLock;
  std::condition_variable netReadCv;

  int netWriteCnt = 0;
  std::mutex netWriteLock;
  std::condition_variable netWriteCv;

  std::vector<ServerQueryPacket> serverQueryQueue;
  std::vector<ServerQueryPacket> serverQueryQueuePrio;
  // Control the rate at which query requests are sent to the server to avoid
  // excessive server pressure caused by sending too many requests
  size_t serverQuerySpaceLeft = 0;
  size_t serverQuerySpaceBase = 0;

  int64_t refTime = 0;

  size_t pixelsDataSize = 0;
  char* pixelsDataBuffer = nullptr;
  std::vector<TexturePixels> penddingPixels;
};

}  // namespace inspector