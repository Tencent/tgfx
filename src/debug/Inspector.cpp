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
#include "CompressImage.h"
#include "ProcessUtils.h"
#include "Protocol.h"
#include "Socket.h"
#include "TCPPortProvider.h"
#include "core/utils/Log.h"
#include "lz4.h"
#include "tgfx/core/Clock.h"

namespace tgfx::debug {
Inspector::Inspector()
    : epoch(Clock::Now()), initTime(Clock::Now()), dataBuffer(TargetFrameSize * 3),
      lz4Buf(LZ4Size + sizeof(lz4sz_t)), lz4Stream(LZ4_createStream()), broadcast(BroadcastNum) {
  spawnWorkerThreads();
}

Inspector::~Inspector() {
  shutdown.store(true, std::memory_order_relaxed);
  if (messageThread) {
    messageThread->join();
    messageThread.reset();
  }
  if (compressThread) {
    compressThread->join();
    compressThread.reset();
  }
  broadcast.clear();
  if (lz4Stream) {
    LZ4_freeStream(static_cast<LZ4_stream_t*>(lz4Stream));
    lz4Stream = nullptr;
  }
}

bool Inspector::ShouldExit() {
  return GetInspector().shutdown.load(std::memory_order_relaxed);
}

void Inspector::spawnWorkerThreads() {
  messageThread = std::make_unique<std::thread>(LaunchWorker, this);
  compressThread = std::make_unique<std::thread>(LaunchCompressWorker, this);
  timeBegin.store(Clock::Now(), std::memory_order_relaxed);
}

bool Inspector::handleServerQuery() {
  ServerQueryPacket payload = {};
  if (!sock->readData(&payload, sizeof(payload), 10)) {
    return false;
  }
  auto type = payload.type;
  auto ptr = payload.ptr;
  switch (type) {
    case ServerQuery::String: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), MsgType::StringData);
    }
    case ServerQuery::ValueName: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), MsgType::ValueName);
    }
    default:
      break;
  }
  return true;
}

void Inspector::sendString(uint64_t str, const char* ptr, size_t len, MsgType type) {
  MsgItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = str;

  auto dataLen = static_cast<uint16_t>(len);
  needDataSize(MsgDataSize[static_cast<int>(type)] + sizeof(uint16_t) + dataLen);
  appendDataUnsafe(&item, MsgDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint16_t));
  appendDataUnsafe(ptr, dataLen);
}

void Inspector::sendString(uint64_t str, const char* ptr, MsgType type) {
  sendString(str, ptr, strlen(ptr), type);
}

void Inspector::sendLongString(uint64_t str, const char* ptr, size_t len, MsgType type) {
  ASSERT(type == MsgType::PixelsData);
  MsgItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = str;

  ASSERT(MsgDataSize[static_cast<int>(type)] + sizeof(uint32_t) + len <= TargetFrameSize);
  auto dataLen = static_cast<uint32_t>(len);
  needDataSize(MsgDataSize[static_cast<int>(type)] + sizeof(uint32_t) + dataLen);
  appendDataUnsafe(&item, MsgDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint32_t));
  appendDataUnsafe(ptr, dataLen);
}

void Inspector::worker() {
  std::string addr = "255.255.255.255";
  auto dataPort = TCPPortProvider::Get().getValidPort();
  if (dataPort == 0) {
    return;
  }
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);

  while (timeBegin.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto welcome = WelcomeMessage();
  welcome.initBegin = GetInspector().initTime;
  welcome.initEnd = timeBegin.load(std::memory_order_relaxed);

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
  for (uint16_t i = 0; i < BroadcastNum; i++) {
    broadcast[i] = std::make_shared<UdpBroadcast>();
    if (!broadcast[i]->openConnect(addr.c_str(), broadcastPort + i)) {
      broadcast[i].reset();
    }
  }

  size_t broadcastLen = 0;
  auto broadcastMsg =
      GetBroadcastMessage(procname, pnsz, broadcastLen, dataPort, ToolType::FrameCapture);
  long long lastBroadcast = 0;
  while (true) {
    welcome.refTime = refTimeThread;
    while (true) {
      if (ShouldExit()) {
        for (uint16_t i = 0; i < BroadcastNum; i++) {
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
        for (uint16_t i = 0; i < BroadcastNum; i++) {
          if (broadcast[i]) {
            programNameLock.lock();
            if (programName) {
              broadcastMsg = GetBroadcastMessage(programName, strlen(programName), broadcastLen,
                                                 dataPort, ToolType::FrameCapture);
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

    for (uint16_t i = 0; i < BroadcastNum; i++) {
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

void Inspector::compressWorker() {
  while (true) {
    if (ShouldExit()) {
      return;
    }
    auto imageItem = ImageItem();
    while (imageQueue.try_dequeue(imageItem)) {
      const auto width = imageItem.width;
      const auto height = imageItem.height;
      const auto compressSize = static_cast<size_t>(width * height / 2);
      auto compressBuffer = static_cast<uint8_t*>(malloc(compressSize));
      CompressImage(imageItem.image->bytes(), compressBuffer, width, height);
      imageItem.image->release();

      auto item = MsgItem();
      item.hdr.type = MsgType::TextureData;
      item.textureData.texturePtr = imageItem.texturePtr;
      item.textureData.width = imageItem.width;
      item.textureData.height = imageItem.height;
      item.textureData.rowBytes = imageItem.rowBytes;
      item.textureData.format = imageItem.format;
      item.textureData.pixels = reinterpret_cast<uint64_t>(compressBuffer);
      QueueSerialFinish(item);
    }
  }
}

bool Inspector::appendData(const void* data, size_t len) {
  const auto ret = needDataSize(len);
  appendDataUnsafe(data, len);
  return ret;
}

bool Inspector::needDataSize(size_t len) {
  bool ret = true;
  if (static_cast<size_t>(dataBufferOffset - dataBufferStart) + len > TargetFrameSize) {
    ret = commitData();
  }
  return ret;
}

void Inspector::appendDataUnsafe(const void* data, size_t len) {
  memcpy(dataBuffer.bytes() + dataBufferOffset, data, len);
  dataBufferOffset += static_cast<int>(len);
}

bool Inspector::commitData() {
  bool ret = sendData(static_cast<const char*>(dataBuffer.data()) + dataBufferStart,
                      static_cast<size_t>(dataBufferOffset - dataBufferStart));
  if (dataBufferOffset > TargetFrameSize * 2) {
    dataBufferOffset = 0;
  }
  dataBufferStart = dataBufferOffset;
  return ret;
}

bool Inspector::sendData(const char* data, size_t len) {
  const auto lz4sz = LZ4_compress_fast_continue(static_cast<LZ4_stream_t*>(lz4Stream), data,
                                                static_cast<char*>(lz4Buf.data()) + sizeof(lz4sz_t),
                                                static_cast<int>(len), LZ4Size, 1);
  memcpy(lz4Buf.data(), &lz4sz, sizeof(lz4sz));
  return sock->sendData(lz4Buf.bytes(), static_cast<size_t>(lz4sz) + sizeof(lz4sz_t)) != -1;
}

bool Inspector::confirmProtocol() {
  char shibboleth[HandshakeShibbolethSize] = "";
  auto res = sock->readRaw(shibboleth, HandshakeShibbolethSize, 2000);
  if (!res || memcmp(shibboleth, HandshakeShibboleth, HandshakeShibbolethSize) != 0) {
    sock.reset();
    return false;
  }
  uint32_t protocolVersion = 0;
  res = sock->readRaw(&protocolVersion, sizeof(protocolVersion), 2000);
  if (!res) {
    sock.reset();
    return false;
  }
  if (protocolVersion != ProtocolVersion) {
    auto status = HandshakeStatus::HandshakeProtocolMismatch;
    sock->sendData(&status, sizeof(status));
    sock.reset();
    return false;
  }
  return true;
}

void Inspector::handleConnect(const WelcomeMessage& welcome) {
  isConnect.store(true, std::memory_order_release);
  auto handshake = HandshakeStatus::HandshakeWelcome;
  sock->sendData(&handshake, sizeof(handshake));

  LZ4_resetStream(static_cast<LZ4_stream_t*>(lz4Stream));
  sock->sendData(&welcome, sizeof(welcome));

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
          MsgItem ka = {};
          ka.hdr.type = MsgType::KeepAlive;
          appendData(&ka, MsgDataSize[ka.hdr.idx]);
          if (!commitData()) {
            break;
          }
          keepAlive = 0;
        } else if (!sock->hasData()) {
          ++keepAlive;
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      } else {
        keepAlive = 0;
      }

      bool connActive = true;
      while (sock->hasData()) {
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
    MsgItem item = {};
    while (serialConcurrentQueue.try_dequeue(item)) {
      auto idx = item.hdr.idx;
      switch (static_cast<MsgType>(idx)) {
        case MsgType::TextureData: {
          auto ptr = item.textureData.pixels;
          const auto w = item.textureData.width;
          const auto h = item.textureData.height;
          const auto csz = static_cast<size_t>(w * h / 2);
          sendLongString(ptr, (const char*)ptr, csz, MsgType::PixelsData);
          free((void*)ptr);
          break;
        }
        case MsgType::OperateBegin: {
          auto t = item.operateBegin.usTime;
          auto dt = t - refThread;
          refThread = t;
          item.operateBegin.usTime = dt;
          break;
        }
        case MsgType::OperateEnd: {
          auto t = item.operateEnd.usTime;
          auto dt = t - refThread;
          refThread = t;
          item.operateEnd.usTime = dt;
          break;
        }
        default:
          break;
      }
      if (!appendData(&item, MsgDataSize[idx])) {
        return DequeueStatus::ConnectionLost;
      }
    }
    refTimeThread = refThread;
  } else {
    return DequeueStatus::QueueEmpty;
  }
  return DequeueStatus::DataDequeued;
}
}  // namespace tgfx::debug