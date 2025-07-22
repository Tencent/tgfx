/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "ProcessUtils.h"
#include "Protocol.h"
#include "Socket.h"
#include "TCPPortProvider.h"
#include "lz4.h"

namespace inspector {
Inspector::Inspector()
    : epoch(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count()),
      initTime(GetTime()), dataBuffer(static_cast<char*>(malloc(TargetFrameSize * 3))),
      lz4Buf(static_cast<char*>(malloc(LZ4Size + sizeof(lz4sz_t)))), lz4Stream(LZ4_createStream()),
      broadcast(broadcastNum) {
  spawnWorkerThreads();
}

Inspector::~Inspector() {
  shutdown.store(true, std::memory_order_relaxed);
  if (messageThread) {
    messageThread->join();
    messageThread.reset();
  }
  broadcast.clear();
  if (lz4Buf) {
    free(lz4Buf);
    lz4Buf = nullptr;
  }
  if (dataBuffer) {
    free(dataBuffer);
    dataBuffer = nullptr;
  }
  if (lz4Stream) {
    LZ4_freeStream(static_cast<LZ4_stream_t*>(lz4Stream));
    lz4Stream = nullptr;
  }
}
void Inspector::spawnWorkerThreads() {
  messageThread = std::make_unique<std::thread>(LaunchWorker, this);
  timeBegin.store(GetTime(), std::memory_order_relaxed);
}

bool Inspector::handleServerQuery() {
  ServerQueryPacket payload = {};
  if (!sock->readData(&payload, sizeof(payload), 10)) {
    return false;
  }
  auto type = payload.type;
  auto ptr = payload.ptr;

  switch (type) {
    case ServerQuery::ServerQueryString: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), QueueType::StringData);
    }
    case ServerQuery::ServerQueryValueName: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), QueueType::ValueName);
    }
    case ServerQuery::ServerQueryDisconnect:
    default: {
      break;
    }
  }
  return true;
}

bool Inspector::ShouldExit() {
  return InspectorSingleton::GetInstance()->shutdown.load(std::memory_order_relaxed);
}

void Inspector::sendString(uint64_t str, const char* ptr, size_t len, QueueType type) {
  QueueItem item = {};
  MemWrite(&item.hdr.type, type);
  MemWrite(&item.stringTransfer.ptr, str);

  auto l16 = static_cast<uint16_t>(len);
  needDataSize(QueueDataSize[static_cast<int>(type)] + sizeof(l16) + l16);

  appendDataUnsafe(&item, QueueDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&l16, sizeof(l16));
  appendDataUnsafe(ptr, l16);
}

void Inspector::worker() {
  std::string addr = "255.255.255.255";
  auto dataPort = TCPPortProvider::Get().getValidPort();
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);

  while (timeBegin.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto welcome = WelcomeMessage{};
  MemWrite(&welcome.initBegin, InspectorSingleton::GetInstance()->initTime);
  MemWrite(&welcome.initEnd, timeBegin.load(std::memory_order_relaxed));

  auto listen = ListenSocket{};
  bool isListening = false;
  for (uint16_t i = 0; i < 20; ++i) {
    if (listen.listenSock(dataPort + i, 4)) {
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
    broadcast[i] = std::make_shared<UdpBroadcast>();
    if (!broadcast[i]->openConnect(addr.c_str(), broadcastPort + i)) {
      broadcast[i].reset();
    }
  }

  size_t broadcastLen = 0;
  auto broadcastMsg =
      GetBroadcastMessage(procname, pnsz, broadcastLen, dataPort, MsgType::FrameCapture);
  long long lastBroadcast = 0;
  while (true) {
    MemWrite(&welcome.refTime, refTimeThread);
    while (true) {
      if (ShouldExit()) {
        for (uint16_t i = 0; i < broadcastNum; i++) {
          if (broadcast[i]) {
            broadcastMsg.activeTime = -1;
            broadcast[i]->sendData(broadcastPort + i, &broadcastMsg, broadcastLen);
          }
        }
        return;
      }
      sock = listen.acceptSock();
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
                                                 dataPort, MsgType::FrameCapture);
              programName = nullptr;
            }
            programNameLock.unlock();

            const auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
            broadcastMsg.activeTime = static_cast<int32_t>(ts - epoch);
            broadcast[i]->sendData(broadcastPort + i, &broadcastMsg, broadcastLen);
          }
        }
      }
    }
    for (uint16_t i = 0; i < broadcastNum; i++) {
      if (broadcast[i]) {
        lastBroadcast = 0;
        broadcastMsg.activeTime = -1;
        broadcast[i]->sendData(broadcastPort + i, &broadcastMsg, broadcastLen);
      }
    }
    if (!confirmProtocol()) {
      continue;
    }
    handleConnect(welcome);
    if (ShouldExit()) {
      break;
    }
    isConnect.store(false, std::memory_order_release);
    sock.reset();
  }
}

bool Inspector::commitData() {
  bool ret = sendData(dataBuffer + dataBufferStart, size_t(dataBufferOffset - dataBufferStart));
  if (dataBufferOffset > TargetFrameSize * 2) {
    dataBufferOffset = 0;
  }
  dataBufferStart = dataBufferOffset;
  return ret;
}

bool Inspector::sendData(const char* data, size_t len) {
  const auto lz4sz = LZ4_compress_fast_continue(static_cast<LZ4_stream_t*>(lz4Stream), data,
                                                lz4Buf + sizeof(lz4sz_t), (int)len, LZ4Size, 1);
  memcpy(lz4Buf, &lz4sz, sizeof(lz4sz));
  return sock->sendData(lz4Buf, size_t(lz4sz) + sizeof(lz4sz_t)) != -1;
}

bool Inspector::confirmProtocol() {
  char shibboleth[HandshakeShibbolethSize] = "";
  auto res = this->sock->readRaw(shibboleth, HandshakeShibbolethSize, 2000);
  if (!res || memcmp(shibboleth, HandshakeShibboleth, HandshakeShibbolethSize) != 0) {
    sock.reset();
    return false;
  }
  uint32_t protocolVersion = 0;
  res = this->sock->readRaw(&protocolVersion, sizeof(protocolVersion), 2000);
  if (!res) {
    sock.reset();
    return false;
  }
  if (protocolVersion != ProtocolVersion) {
    auto status = HandshakeStatus::HandshakeProtocolMismatch;
    this->sock->sendData(&status, sizeof(status));
    sock.reset();
    return false;
  }
  return true;
}

void Inspector::handleConnect(WelcomeMessage& welcome) {
  isConnect.store(true, std::memory_order_release);
  auto handshake = HandshakeStatus::HandshakeWelcome;
  sock->sendData(&handshake, sizeof(handshake));

  LZ4_resetStream(static_cast<LZ4_stream_t*>(lz4Stream));
  this->sock->sendData(&welcome, sizeof(welcome));

  auto keepAlive = 0;
  while (true) {
    const auto serialStatus = dequeueSerial();
    if (serialStatus == DequeueStatus::ConnectionLost) {
      break;
    } else if (serialStatus == DequeueStatus::QueueEmpty) {
      if (ShouldExit()) {
        break;
      }
      if (dataBufferOffset != dataBufferStart) {
        if (!commitData()) {
          break;
        }
        if (keepAlive == 500) {
          QueueItem ka = {};
          ka.hdr.type = QueueType::KeepAlive;
          appendData(&ka, QueueDataSize[ka.hdr.idx]);
          if (!commitData()) {
            break;
          }

          keepAlive = 0;
        } else if (!this->sock->hasData()) {
          ++keepAlive;
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      } else {
        keepAlive = 0;
      }

      bool connActive = true;
      while (this->sock->hasData()) {
        connActive = handleServerQuery();
        if (!connActive) {
          break;
        }
      }
      if (!connActive) {
        break;
      }
    }
  }
}

Inspector::DequeueStatus Inspector::dequeueSerial() {
  const auto queueSize = serialConcurrentQueue.size_approx();
  if (queueSize > 0) {
    auto refThread = refTimeThread;
    auto item = QueueItem{};
    while (serialConcurrentQueue.try_dequeue(item)) {
      auto idx = item.hdr.idx;
      switch ((QueueType)idx) {
        case QueueType::OperateBegin: {
          auto t = MemRead<int64_t>(&item.operateBegin.nsTime);
          auto dt = t - refThread;
          refThread = t;
          MemWrite(&item.operateBegin.nsTime, dt);
          break;
        }
        case QueueType::OperateEnd: {
          auto t = MemRead<int64_t>(&item.operateEnd.nsTime);
          auto dt = t - refThread;
          refThread = t;
          MemWrite(&item.operateEnd.nsTime, dt);
          break;
        }
        default:
          break;
      }
      if (!appendData(&item, QueueDataSize[idx])) {
        return DequeueStatus::ConnectionLost;
      }
    }
    refTimeThread = refThread;
  } else {
    return DequeueStatus::QueueEmpty;
  }
  return DequeueStatus::DataDequeued;
}
}  // namespace inspector