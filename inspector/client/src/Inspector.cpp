/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//
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

#include "Inspector.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <new>
#include "Alloc.h"
#include "LZ4.h"
#include "Protocol.h"
#include "Socket.h"
#include "Yield.h"

namespace inspector {

struct SourceLocationData {
  const char* name;
  const char* function;
};

struct InspectorData {
  int64_t initTime = Inspector::GetTime();
  Inspector inspector;
};

static std::atomic<int> inspectorDataLock{0};
static std::atomic<InspectorData*> inspectorData{nullptr};
static Inspector* s_instance = nullptr;

static InspectorData& GetInspectorData() {
  auto ptr = inspectorData.load(std::memory_order_acquire);
  if (!ptr) {
    int expected = 0;
    while (!inspectorDataLock.compare_exchange_weak(expected, 1, std::memory_order_release,
                                                    std::memory_order_relaxed)) {
      expected = 0;
      YieldThread();
    }
    ptr = inspectorData.load(std::memory_order_acquire);
    if (!ptr) {
      ptr = (InspectorData*)inspectorMalloc(sizeof(InspectorData));
      new (ptr) InspectorData();
      inspectorData.store(ptr, std::memory_order_release);
    }
    inspectorDataLock.store(0, std::memory_order_release);
  }
  return *ptr;
}

Inspector& GetInspector() {
  return GetInspectorData().inspector;
}

int64_t GetInitTime() {
  return GetInspectorData().initTime;
}

Inspector::Inspector()
    : epoch(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count()),
      dataBuffer((char*)inspectorMalloc(TargetFrameSize * 3)),
      lz4Buf((char*)inspectorMalloc(LZ4Size + sizeof(lz4sz_t))), shutdown(false), timeBegin(0),
      frameCount(0), isConnect(false), refTimeThread(0), serialQueue(1024 * 1024),
      serialDequeue(1024 * 1024), lz4Stream(LZ4_createStream()), dataBufferOffset(0),
      dataBufferStart(0) {
  s_instance = this;
  SpawnWorkerThreads();
}

Inspector::~Inspector() {
  shutdown.store(true, std::memory_order_relaxed);
  messageThread->join();
  inspectorFree(messageThread);
  inspectorFree(lz4Buf);
  inspectorFree(dataBuffer);
  LZ4_freeStream((LZ4_stream_t*)lz4Stream);

  if (sock) {
    sock->~Socket();
    inspectorFree(sock);
  }
  for (uint16_t i = 0; i < broadcastNum; i++) {
    if (broadcast[i]) {
      broadcast[i]->~UdpBroadcast();
      inspectorFree(broadcast[i]);
    }
  }
}

void Inspector::SpawnWorkerThreads() {
  messageThread = (std::thread*)inspectorMalloc(sizeof(std::thread));
  new (messageThread) std::thread(LaunchWorker, this);

  timeBegin.store(GetTime(), std::memory_order_relaxed);
}

bool Inspector::HandleServerQuery() {
  ServerQueryPacket payload;
  if (!this->sock->Read(&payload, sizeof(payload), 10)) {
    return false;
  }
  uint8_t type;
  uint64_t ptr;
  memcpy(&type, &payload.type, sizeof(payload.type));
  memcpy(&ptr, &payload.ptr, sizeof(payload.ptr));

  switch (type) {
    case ServerQueryString: {
      SendString(ptr, (const char*)ptr, QueueType::StringData);
    }
    case ServerQueryFrameName: {
      break;
    }
    case ServerQueryValueName: {
      SendString(ptr, (const char*)ptr, QueueType::ValueName);
    }
    case ServerQueryDisconnect: {
      break;
    }
    default: {
      assert(false);
      break;
    }
  }

  return true;
}

bool Inspector::ShouldExit() {
  return s_instance->shutdown.load(std::memory_order_relaxed);
}

void Inspector::SendString(uint64_t str, const char* ptr, size_t len, QueueType type) {
  assert(type == QueueType::StringData || type == QueueType::ValueName);

  QueueItem item;
  MemWrite(&item.hdr.type, type);
  MemWrite(&item.stringTransfer.ptr, str);

  assert(len <= std::numeric_limits<uint16_t>::max());
  auto l16 = uint16_t(len);
  NeetDataSize(QueueDataSize[static_cast<int>(type)] + sizeof(l16) + l16);

  AppendDataUnsafe(&item, QueueDataSize[static_cast<int>(type)]);
  AppendDataUnsafe(&l16, sizeof(l16));
  AppendDataUnsafe(ptr, l16);
}

void Inspector::Worker() {
  const char* addr = "255.255.255.255";
  uint16_t dataPort = 8086;
  const auto broadcastPort = 8086;
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);

  while (timeBegin.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  WelcomeMessage welcome;
  MemWrite(&welcome.initBegin, GetInitTime());
  MemWrite(&welcome.initEnd, timeBegin.load(std::memory_order_relaxed));

  ListenSocket listen;
  bool isListening = false;
  for (uint16_t i = 0; i < 20; ++i) {
    if (listen.Listen(dataPort + i, 4)) {
      dataPort += i;
      isListening = true;
      break;
    }
  }
  if (!isListening) {
    while (true) {
      if (ShouldExit()) {
        shutdown.store(true, std::memory_order_relaxed);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  for (uint16_t i = 0; i < broadcastNum; i++) {
    broadcast[i] = (UdpBroadcast*)inspectorMalloc(sizeof(UdpBroadcast));
    new (broadcast[i]) UdpBroadcast();
    if (!broadcast[i]->Open(addr, broadcastPort + i)) {
      broadcast[i]->~UdpBroadcast();
      inspectorFree(broadcast[i]);
      broadcast[i] = nullptr;
    }
  }

  size_t broadcastLen = 0;
  auto broadcastMsg = GetBroadcastMessage(procname, pnsz, broadcastLen, dataPort, FrameCapture);
  long long lastBroadcast = 0;
  while (true) {
    MemWrite(&welcome.refTime, refTimeThread);
    while (true) {
      if (ShouldExit()) {
        for (uint16_t i = 0; i < broadcastNum; i++) {
          if (broadcast[i]) {
            broadcastMsg.activeTime = -1;
            broadcast[i]->Send(broadcastPort + i, &broadcastMsg, broadcastLen);
          }
        }
        return;
      }
      this->sock = listen.Accept();
      if (sock) {
        break;
      }
      const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      if (t - lastBroadcast > 3000000000) {
        lastBroadcast = t;
        for (uint16_t i = 0; i < broadcastNum; i++) {
          if (broadcast[i]) {
            programNameLock.lock();
            if (programName) {
              broadcastMsg = GetBroadcastMessage(programName, strlen(programName), broadcastLen,
                                                 dataPort, FrameCapture);
              programName = nullptr;
            }
            programNameLock.unlock();

            const auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
            broadcastMsg.activeTime = int32_t(ts - epoch);
            assert(broadcastMsg.activeTime >= 0);
            broadcast[i]->Send(broadcastPort + i, &broadcastMsg, broadcastLen);
          }
        }
      }
    }
    for (uint16_t i = 0; i < broadcastNum; i++) {
      if (broadcast[i]) {
        lastBroadcast = 0;
        broadcastMsg.activeTime = -1;
        broadcast[i]->Send(broadcastPort + i, &broadcastMsg, broadcastLen);
      }
    }

    {
      char shibboleth[HandshakeShibbolethSize];
      auto res = this->sock->ReadRaw(shibboleth, HandshakeShibbolethSize, 2000);
      if (!res || memcmp(shibboleth, HandshakeShibboleth, HandshakeShibbolethSize) != 0) {
        this->sock->~Socket();
        inspectorFree(this->sock);
        this->sock = nullptr;
        continue;
      }

      uint32_t protocolVersion;
      res = this->sock->ReadRaw(&protocolVersion, sizeof(protocolVersion), 2000);
      if (!res) {
        this->sock->~Socket();
        inspectorFree(this->sock);
        this->sock = nullptr;
        continue;
      }

      if (protocolVersion != ProtocolVersion) {
        HandshakeStatus status = HandshakeProtocolMismatch;
        this->sock->Send(&status, sizeof(status));
        this->sock->~Socket();
        inspectorFree(this->sock);
        this->sock = nullptr;
        continue;
      }
    }
    isConnect.store(true, std::memory_order_release);
    HandshakeStatus handshake = HandshakeWelcome;
    sock->Send(&handshake, sizeof(handshake));

    LZ4_resetStream((LZ4_stream_t*)lz4Stream);
    this->sock->Send(&welcome, sizeof(welcome));

    int keepAlive = 0;
    while (true) {
      const auto serialStatus = DequeueSerial();
      if (serialStatus == DequeueStatus::ConnectionLost) {
        break;
      } else if (serialStatus == DequeueStatus::QueueEmpty) {
        if (ShouldExit()) {
          break;
        }
        if (dataBufferOffset != dataBufferStart) {
          if (!CommitData()) {
            break;
          }
          if (keepAlive == 500) {
            QueueItem ka;
            ka.hdr.type = QueueType::KeepAlive;
            AppendData(&ka, QueueDataSize[ka.hdr.idx]);
            if (!CommitData()) {
              break;
            }

            keepAlive = 0;
          } else if (!this->sock->HasData()) {
            ++keepAlive;
            std::this_thread::sleep_for(std::chrono::microseconds(10));
          }
        } else {
          keepAlive = 0;
        }

        bool connActive = true;
        while (this->sock->HasData()) {
          connActive = HandleServerQuery();
          if (!connActive) {
            break;
          }
        }
        if (!connActive) {
          break;
        }
      }
    }
    if (ShouldExit()) {
      break;
    }

    isConnect.store(false, std::memory_order_release);
    this->sock->~Socket();
    inspectorFree(this->sock);
    this->sock = nullptr;
  }
}

bool Inspector::CommitData() {
  bool ret = SendData(dataBuffer + dataBufferStart, size_t(dataBufferOffset - dataBufferStart));
  if (dataBufferOffset > TargetFrameSize * 2) {
    dataBufferOffset = 0;
  }
  dataBufferStart = dataBufferOffset;
  return ret;
}

bool Inspector::SendData(const char* data, size_t len) {
  const auto lz4sz = LZ4_compress_fast_continue((LZ4_stream_t*)lz4Stream, data,
                                                lz4Buf + sizeof(lz4sz_t), (int)len, LZ4Size, 1);
  memcpy(lz4Buf, &lz4sz, sizeof(lz4sz));
  return sock->Send(lz4Buf, size_t(lz4sz) + sizeof(lz4sz_t)) != -1;
}

Inspector::DequeueStatus Inspector::DequeueSerial() {
  {
    std::lock_guard<std::mutex> lockGuard(serialLock);
    if (!serialQueue.empty()) {
      serialQueue.swap(serialDequeue);
    }
  }

  const auto queueSize = serialDequeue.size();
  if (queueSize > 0) {
    auto refThread = refTimeThread;
    auto item = serialDequeue.data();
    auto end = item + queueSize;
    while (item != end) {
      auto idx = MemRead<uint8_t>(&item->hdr.idx);
      switch ((QueueType)idx) {
        case QueueType::OperateBegin: {
          auto t = MemRead<int64_t>(&item->operateBegin.time);
          auto dt = t - refThread;
          refThread = t;
          MemWrite(&item->operateBegin.time, dt);
          break;
        }
        case QueueType::OperateEnd: {
          auto t = MemRead<int64_t>(&item->operateEnd.time);
          auto dt = t - refThread;
          refThread = t;
          MemWrite(&item->operateEnd.time, dt);
          break;
        }
        default:
          break;
      }
      if (!AppendData(item, QueueDataSize[idx])) {
        return DequeueStatus::ConnectionLost;
      }
      ++item;
    }
    refTimeThread = refThread;
    serialDequeue.clear();
  } else {
    return DequeueStatus::QueueEmpty;
  }
  return DequeueStatus::DataDequeued;
}
}  // namespace inspector