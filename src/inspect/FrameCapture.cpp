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
#include "FrameCapture.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include "ProcessUtils.h"
#include "Protocol.h"
#include "Socket.h"
#include "TCPPortProvider.h"
#include "lz4.h"
#include "tgfx/core/Clock.h"

namespace tgfx::inspect {
FrameCapture::FrameCapture()
    : epoch(Clock::Now()), initTime(Clock::Now()), dataBuffer(TargetFrameSize * 3),
      lz4Buf(LZ4Size + sizeof(int)), lz4Stream(LZ4_createStream()), broadcast(BroadcastCount) {
  spawnWorkerThreads();
}

FrameCapture::~FrameCapture() {
  shutdown.store(true, std::memory_order_relaxed);
  if (messageThread) {
    messageThread->join();
    messageThread.reset();
  }
  broadcast.clear();
  if (lz4Stream) {
    LZ4_freeStream(static_cast<LZ4_stream_t*>(lz4Stream));
    lz4Stream = nullptr;
  }
}

void FrameCapture::QueueSerialFinish(const FrameCaptureMessageItem& item) {
  GetInspector().serialConcurrentQueue.enqueue(item);
}

void FrameCapture::SendAttributeData(const char* name, const Rect& rect) {
  float value[4] = {rect.left, rect.right, rect.top, rect.bottom};
  SendAttributeData(name, value, 4);
}

void FrameCapture::SendAttributeData(const char* name, const Matrix& matrix) {
  float value[6] = {1, 0, 0, 0, 1, 0};
  value[0] = matrix.getScaleX();
  value[1] = matrix.getSkewX();
  value[2] = matrix.getTranslateX();
  value[3] = matrix.getSkewY();
  value[4] = matrix.getScaleY();
  value[5] = matrix.getTranslateY();
  SendAttributeData(name, value, 6);
}

void FrameCapture::SendAttributeData(const char* name, const std::optional<Matrix>& matrix) {
  auto value = Matrix::MakeAll(1, 0, 0, 0, 1, 0);
  if (matrix.has_value()) {
    value = matrix.value();
  }
  SendAttributeData(name, value);
}

void FrameCapture::SendAttributeData(const char* name, const Color& color) {
  auto r = static_cast<uint8_t>(color.red * 255.f);
  auto g = static_cast<uint8_t>(color.green * 255.f);
  auto b = static_cast<uint8_t>(color.blue * 255.f);
  auto a = static_cast<uint8_t>(color.alpha * 255.f);
  auto value = static_cast<uint32_t>(r | g << 8 | b << 16 | a << 24);
  SendAttributeData(name, value, FrameCaptureMessageType::ValueDataColor);
}

void FrameCapture::SendAttributeData(const char* name, const std::optional<Color>& color) {
  auto value = Color::FromRGBA(255, 255, 255, 255);
  if (color.has_value()) {
    value = color.value();
  }
  SendAttributeData(name, value);
}

void FrameCapture::SendFrameMark(const char* name) {
  if (!name) {
    GetInspector().frameCount.fetch_add(1, std::memory_order_relaxed);
  }
  auto item = FrameCaptureMessageItem();
  item.hdr.type = FrameCaptureMessageType::FrameMarkMessage;
  item.frameMark.usTime = Clock::Now();
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, int val) {
  auto item = FrameCaptureMessageItem();
  item.hdr.type = FrameCaptureMessageType::ValueDataInt;
  item.attributeDataInt.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataInt.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, float val) {
  auto item = FrameCaptureMessageItem();
  item.hdr.type = FrameCaptureMessageType::ValueDataFloat;
  item.attributeDataFloat.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataFloat.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, bool val) {
  auto item = FrameCaptureMessageItem();
  item.hdr.type = FrameCaptureMessageType::ValueDataBool;
  item.attributeDataBool.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataBool.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, uint8_t val, uint8_t type) {
  auto item = FrameCaptureMessageItem();
  item.hdr.type = FrameCaptureMessageType::ValueDataEnum;
  item.attributeDataEnum.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataEnum.value = static_cast<uint16_t>(type << 8 | val);
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, uint32_t val, FrameCaptureMessageType type) {
  auto item = FrameCaptureMessageItem();
  item.hdr.type = type;
  item.attributeDataUint32.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataUint32.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, float* val, int size) {
  if (size == 4) {
    auto item = FrameCaptureMessageItem();
    item.hdr.type = FrameCaptureMessageType::ValueDataFloat4;
    item.attributeDataFloat4.name = reinterpret_cast<uint64_t>(name);
    memcpy(item.attributeDataFloat4.value, val, static_cast<size_t>(size) * sizeof(float));
    QueueSerialFinish(item);
  } else if (size == 6) {
    auto item = FrameCaptureMessageItem();
    item.hdr.type = FrameCaptureMessageType::ValueDataMat3;
    item.attributeDataMat4.name = reinterpret_cast<uint64_t>(name);
    memcpy(item.attributeDataMat4.value, val, static_cast<size_t>(size) * sizeof(float));
    QueueSerialFinish(item);
  }
}

void FrameCapture::LaunchWorker(FrameCapture* inspector) {
  inspector->worker();
}

bool FrameCapture::ShouldExit() {
  return GetInspector().shutdown.load(std::memory_order_relaxed);
}

void FrameCapture::spawnWorkerThreads() {
  messageThread = std::make_unique<std::thread>(LaunchWorker, this);
  timeBegin.store(Clock::Now(), std::memory_order_relaxed);
}

bool FrameCapture::handleServerQuery() {
  ServerQueryPacket payload = {};
  if (!sock->readData(&payload, sizeof(payload), 10)) {
    return false;
  }
  auto type = payload.type;
  auto ptr = payload.ptr;
  switch (type) {
    case ServerQuery::String: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), FrameCaptureMessageType::StringData);
    }
    case ServerQuery::ValueName: {
      sendString(ptr, reinterpret_cast<const char*>(ptr), FrameCaptureMessageType::ValueName);
    }
    default:
      break;
  }
  return true;
}

void FrameCapture::sendString(uint64_t str, const char* ptr, size_t len,
                              FrameCaptureMessageType type) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = str;

  auto dataLen = static_cast<uint16_t>(len);
  needDataSize(FrameCaptureMessageDataSize[static_cast<int>(type)] + sizeof(uint16_t) + dataLen);
  appendDataUnsafe(&item, FrameCaptureMessageDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint16_t));
  appendDataUnsafe(ptr, dataLen);
}

void FrameCapture::sendString(uint64_t str, const char* ptr, FrameCaptureMessageType type) {
  sendString(str, ptr, strlen(ptr), type);
}

void FrameCapture::worker() {
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
  for (uint16_t i = 0; i < BroadcastCount; i++) {
    broadcast[i] = std::make_shared<UDPBroadcast>();
    if (!broadcast[i]->openConnect(addr.c_str(), BroadcastPort + i)) {
      broadcast[i].reset();
    }
  }

  size_t broadcastLen = 0;
  auto broadcastMessage =
      GetBroadcastMessage(procname, pnsz, broadcastLen, dataPort, ToolType::FrameCapture);
  long long lastBroadcast = 0;
  while (true) {
    welcome.refTime = refTimeThread;
    while (true) {
      if (ShouldExit()) {
        for (uint16_t i = 0; i < BroadcastCount; i++) {
          if (broadcast[i]) {
            broadcastMessage.activeTime = -1;
            broadcast[i]->sendData(BroadcastPort + i, &broadcastMessage, broadcastLen);
          }
        }
        return;
      }

      sock = listen.acceptSock();
      if (sock) {
        break;
      }

      const auto currentTime = Clock::Now();
      if (currentTime - lastBroadcast > BroadcastHeartbeatUSTime) {
        lastBroadcast = currentTime;
        for (uint16_t i = 0; i < BroadcastCount; i++) {
          if (broadcast[i]) {
            programNameLock.lock();
            if (programName) {
              broadcastMessage = GetBroadcastMessage(programName, strlen(programName), broadcastLen,
                                                     dataPort, ToolType::FrameCapture);
              programName = nullptr;
            }
            programNameLock.unlock();

            const auto activeTime = Clock::Now();
            broadcastMessage.activeTime = static_cast<int32_t>(activeTime - epoch);
            broadcast[i]->sendData(BroadcastPort + i, &broadcastMessage, broadcastLen);
          }
        }
      }
    }

    for (uint16_t i = 0; i < BroadcastCount; i++) {
      if (broadcast[i]) {
        lastBroadcast = 0;
        broadcastMessage.activeTime = -1;
        broadcast[i]->sendData(BroadcastPort + i, &broadcastMessage, broadcastLen);
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

bool FrameCapture::appendData(const void* data, size_t len) {
  const auto result = needDataSize(len);
  appendDataUnsafe(data, len);
  return result;
}

bool FrameCapture::needDataSize(size_t len) {
  bool result = true;
  if (static_cast<size_t>(dataBufferOffset - dataBufferStart) + len > TargetFrameSize) {
    result = commitData();
  }
  return result;
}

void FrameCapture::appendDataUnsafe(const void* data, size_t len) {
  memcpy(dataBuffer.bytes() + dataBufferOffset, data, len);
  dataBufferOffset += static_cast<int>(len);
}

bool FrameCapture::commitData() {
  bool result = sendData(static_cast<const char*>(dataBuffer.data()) + dataBufferStart,
                         static_cast<size_t>(dataBufferOffset - dataBufferStart));
  if (dataBufferOffset > TargetFrameSize * 2) {
    dataBufferOffset = 0;
  }
  dataBufferStart = dataBufferOffset;
  return result;
}

bool FrameCapture::sendData(const char* data, size_t len) {
  const auto lz4Size = LZ4_compress_fast_continue(static_cast<LZ4_stream_t*>(lz4Stream), data,
                                                  static_cast<char*>(lz4Buf.data()) + sizeof(int),
                                                  static_cast<int>(len), LZ4Size, 1);
  memcpy(lz4Buf.data(), &lz4Size, sizeof(lz4Size));
  return sock->sendData(lz4Buf.bytes(), static_cast<size_t>(lz4Size) + sizeof(int)) != -1;
}

bool FrameCapture::confirmProtocol() {
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

void FrameCapture::handleConnect(const WelcomeMessage& welcome) {
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
          FrameCaptureMessageItem ka = {};
          ka.hdr.type = FrameCaptureMessageType::KeepAlive;
          appendData(&ka, FrameCaptureMessageDataSize[ka.hdr.idx]);
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

FrameCapture::DequeueStatus FrameCapture::dequeueSerial() {
  const auto queueSize = serialConcurrentQueue.size_approx();
  if (queueSize > 0) {
    auto refThread = refTimeThread;
    FrameCaptureMessageItem item = {};
    while (serialConcurrentQueue.try_dequeue(item)) {
      auto idx = item.hdr.idx;
      switch (static_cast<FrameCaptureMessageType>(idx)) {
        case FrameCaptureMessageType::OperateBegin: {
          auto t = item.operateBegin.usTime;
          auto dt = t - refThread;
          refThread = t;
          item.operateBegin.usTime = dt;
          break;
        }
        case FrameCaptureMessageType::OperateEnd: {
          auto t = item.operateEnd.usTime;
          auto dt = t - refThread;
          refThread = t;
          item.operateEnd.usTime = dt;
          break;
        }
        default:
          break;
      }
      if (!appendData(&item, FrameCaptureMessageDataSize[idx])) {
        return DequeueStatus::ConnectionLost;
      }
    }
    refTimeThread = refThread;
  } else {
    return DequeueStatus::QueueEmpty;
  }
  return DequeueStatus::DataDequeued;
}
}  // namespace tgfx::inspect