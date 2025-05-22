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
#include <cstdint>
#include <string>
#include <thread>
#include "Protocol.h"
#include "Queue.h"
#include "Socket.h"
#include "StringDiscovery.h"
#include "DataContext.h"
#include "DecodeStream.h"

namespace inspector {
class Worker {
 public:
  struct NetBuffer {
    int bufferOffset;
    int size;
  };

  Worker(const char* addr, uint16_t port);
  ~Worker();

  bool Open(const std::string& filePath);
  bool Save(const std::string& filePath);

  int64_t GetFrameTime(const FrameData& fd, size_t idx) const;
  int64_t GetLastTime() const;

 private:
  DecodeStream ReadBodyBytes(DecodeStream* stream);

  void Shutdown();
  void Exec();
  void Network();

  void NewOpTask(std::shared_ptr<OpTaskData> opTask);
  void Query(ServerQuery type, uint64_t data, uint32_t extra = 0);
  void QueryTerminate();
  bool DispatchProcess(const QueueItem& ev, const char*& ptr);
  bool Process(const QueueItem& ev);
  void ProcessOperateBegin(const QueueOperateBegin& ev);
  void ProcessOperateEnd(const QueueOperateEnd& ev);
  void ProcessFrameMark(const QueueFrameMark& ev);

  int64_t TscTime(int64_t tsc) {
    return int64_t(tsc - dataContext.baseTime);
  }
  int64_t TscTime(uint64_t tsc) {
    return int64_t(int64_t(tsc) - dataContext.baseTime);
  }
  int64_t TscPeriod(uint64_t tsc) {
    return int64_t(tsc);
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
  size_t serverQuerySpaceLeft;
  size_t serverQuerySpaceBase;

  int64_t refTime = 0;
};

}  // namespace inspector