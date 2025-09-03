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
#include "core/codecs/jpeg/JpegCodec.h"
#include "core/codecs/png/PngCodec.h"
#include "core/codecs/webp/WebpCodec.h"
#include "core/utils/Log.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/RenderTarget.h"
#include "lz4.h"
#include "tgfx/core/Clock.h"

namespace tgfx::inspect {
FrameCapture::FrameCapture()
    : epoch(Clock::Now()), initTime(Clock::Now()), dataBuffer(TargetFrameSize * 3),
      lz4Handler(LZ4CompressionHandler::Make()), broadcast(BroadcastCount) {
  spawnWorkerThreads();
}

FrameCapture::~FrameCapture() {
  shutdown.store(true, std::memory_order_relaxed);
  if (messageThread) {
    messageThread->join();
    messageThread.reset();
  }
  if (decodeThread) {
    decodeThread->join();
    decodeThread.reset();
  }
  broadcast.clear();
}

uint64_t FrameCapture::NextTextureID() {
  static std::atomic<uint32_t> nextID{1};
  uint32_t id;
  do {
    id = nextID.fetch_add(1, std::memory_order_relaxed);
  } while (id == 0);
  return id;
}

void FrameCapture::QueueSerialFinish(const FrameCaptureMessageItem& item) {
  GetInstance().serialConcurrentQueue.enqueue(item);
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
    GetInstance().frameCount.fetch_add(1, std::memory_order_relaxed);
  }
  GetInstance().currentFrameShouldCaptrue.store(false, std::memory_order_relaxed);
  if (GetInstance().captureFrameCount > 0) {
    GetInstance().captureFrameCount--;
    GetInstance().currentFrameShouldCaptrue.store(true, std::memory_order_relaxed);
  }
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::FrameMarkMessage;
  item.frameMark.captured = CurrentFrameShouldCaptrue();
  item.frameMark.usTime = Clock::Now();
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, int val) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataInt;
  item.attributeDataInt.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataInt.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, float val) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataFloat;
  item.attributeDataFloat.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataFloat.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, bool val) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataBool;
  item.attributeDataBool.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataBool.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, uint8_t val, uint8_t type) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = FrameCaptureMessageType::ValueDataEnum;
  item.attributeDataEnum.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataEnum.value = static_cast<uint16_t>(type << 8 | val);
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, uint32_t val, FrameCaptureMessageType type) {
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.attributeDataUint32.name = reinterpret_cast<uint64_t>(name);
  item.attributeDataUint32.value = val;
  QueueSerialFinish(item);
}

void FrameCapture::SendAttributeData(const char* name, float* val, int size) {
  if (size == 4) {
    FrameCaptureMessageItem item = {};
    item.hdr.type = FrameCaptureMessageType::ValueDataFloat4;
    item.attributeDataFloat4.name = reinterpret_cast<uint64_t>(name);
    memcpy(item.attributeDataFloat4.value, val, static_cast<size_t>(size) * sizeof(float));
    QueueSerialFinish(item);
  } else if (size == 6) {
    FrameCaptureMessageItem item = {};
    item.hdr.type = FrameCaptureMessageType::ValueDataMat3;
    item.attributeDataMat4.name = reinterpret_cast<uint64_t>(name);
    memcpy(item.attributeDataMat4.value, val, static_cast<size_t>(size) * sizeof(float));
    QueueSerialFinish(item);
  }
}

void FrameCapture::SendTextureID(uint64_t texturePtr, FrameCaptureMessageType type) {
  if (!CurrentFrameShouldCaptrue()) {
    return;
  }
  auto textureId = GetInstance().getTextureId(texturePtr);
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.textureSampler.textureId = textureId;
  QueueSerialFinish(item);
}

void FrameCapture::SendInputTextureID(const GPUTexture* texture) {
  SendTextureID(reinterpret_cast<uint64_t>(texture), FrameCaptureMessageType::InputTexture);
}

void FrameCapture::SendOutputTextureID(const GPUTexture* texture) {
  if (!CurrentFrameShouldCaptrue()) {
    return;
  }
  SendTextureID(reinterpret_cast<uint64_t>(texture), FrameCaptureMessageType::OutputTexture);
}

void FrameCapture::SendFrameCaptureTexture(std::shared_ptr<FrameCaptureTexture> frameCaptureTexture,
                                           bool differentEachFrame) {

  uint64_t currentFrame = 0;
  if (differentEachFrame) {
    currentFrame = GetInstance().frameCount.load(std::memory_order_relaxed) + 1;
  }
  auto& textureIds = GetInstance().textureIds;
  auto texture = reinterpret_cast<uint64_t>(frameCaptureTexture->texture());
  auto textureId = NextTextureID();
  textureIds[GetTextureHash(texture, currentFrame)] = textureId;
  frameCaptureTexture->setTextureId(textureId);
  GetInstance().imageQueue.enqueue(std::move(frameCaptureTexture));
}

void FrameCapture::SendFragmentProcessor(
    const std::vector<PlacementPtr<FragmentProcessor>>& fragmentProcessors) {
  for (const auto& processor : fragmentProcessors) {
    FragmentProcessor::Iter fpIter(processor.get());
    while (const auto* subFP = fpIter.next()) {
      for (size_t j = 0; j < subFP->numTextureSamplers(); ++j) {
        auto texture = subFP->textureAt(j);
        SendInputTextureID(texture);
      }
    }
  }
}

void FrameCapture::LaunchWorker(FrameCapture* inspector) {
  inspector->worker();
}

bool FrameCapture::ShouldExit() {
  return GetInstance().shutdown.load(std::memory_order_relaxed);
}

bool FrameCapture::CurrentFrameShouldCaptrue() {
  return GetInstance().currentFrameShouldCaptrue.load(std::memory_order_relaxed);
}

uint64_t FrameCapture::GetTextureHash(uint64_t texturePtr, uint64_t currentFrame) {
  uint64_t combined = texturePtr ^ currentFrame;
  return std::hash<uint64_t>{}(combined);
}

void FrameCapture::spawnWorkerThreads() {
  messageThread = std::make_unique<std::thread>(LaunchWorker, this);
  decodeThread = std::make_unique<std::thread>(LaunchEncodeWorker, this);
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
    case ServerQuery::CaptureFrame: {
      captureFrameCount += payload.extra;
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

void FrameCapture::sendPixelsData(uint64_t str, const char* pixels, size_t len,
                                  FrameCaptureMessageType type) {
  ASSERT(type == FrameCaptureMessageType::PixelsData);
  FrameCaptureMessageItem item = {};
  item.hdr.type = type;
  item.stringTransfer.ptr = str;

  auto dataLen = static_cast<uint32_t>(len);
  needDataSize(FrameCaptureMessageDataSize[static_cast<int>(type)] + sizeof(uint32_t) + dataLen);
  appendDataUnsafe(&item, FrameCaptureMessageDataSize[static_cast<int>(type)]);
  appendDataUnsafe(&dataLen, sizeof(uint32_t));
  appendDataUnsafe(pixels, dataLen);
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
  welcome.initBegin = GetInstance().initTime;
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

void FrameCapture::encodeWorker() {
  while (true) {
    if (ShouldExit()) {
      return;
    }
    std::shared_ptr<FrameCaptureTexture> frameCaputreTexture = nullptr;
    while (imageQueue.try_dequeue(frameCaputreTexture)) {
      const auto width = frameCaputreTexture->width();
      const auto height = frameCaputreTexture->height();
      auto colorType = PixelFormatToColorType(frameCaputreTexture->format());
      auto imageInfo = ImageInfo::Make(width, height, colorType, AlphaType::Premultiplied,
                                       frameCaputreTexture->rowBytes());
#ifdef TGFX_USE_JPEG_ENCODE
      auto encodedFormat = EncodedFormat::JPEG;
#elif TGFX_USE_WEBP_ENCODE
      auto encodeFormat = EncodedFormat::WEBP;
#elif TGFX_USE_PNG_ENCODE
      auto encodeFormat = EncodedFormat::PNG;
#endif
      auto jpgBuffer = ImageCodec::Encode(
          Pixmap(imageInfo, frameCaputreTexture->imageBuffer()->bytes()), encodedFormat, 100);
      auto size = jpgBuffer->size();
      auto pxielsBuffer = static_cast<uint8_t*>(malloc(size));
      memcpy(pxielsBuffer, jpgBuffer->bytes(), size);

      FrameCaptureMessageItem item = {};
      item.hdr.type = FrameCaptureMessageType::TextureData;
      item.textureData.isInput = frameCaputreTexture->isInput();
      item.textureData.textureId = frameCaputreTexture->textureId();
      item.textureData.width = frameCaputreTexture->width();
      item.textureData.height = frameCaputreTexture->height();
      item.textureData.rowBytes = frameCaputreTexture->rowBytes();
      item.textureData.format = frameCaputreTexture->format();
      item.textureData.pixels = reinterpret_cast<uint64_t>(pxielsBuffer);
      item.textureData.pixelsSize = size;
      QueueSerialFinish(item);
    }
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
  if (dataBuffer.size() < len + static_cast<size_t>(dataBufferOffset)) {
    Buffer tempDataBuffer(dataBuffer.size());
    memcpy(tempDataBuffer.bytes(), dataBuffer.bytes(), dataBuffer.size());
    dataBuffer.clear();
    dataBuffer.alloc(len + static_cast<size_t>(dataBufferOffset));
    memcpy(dataBuffer.bytes(), tempDataBuffer.bytes(), tempDataBuffer.size());
  }
  memcpy(dataBuffer.bytes() + dataBufferOffset, data, len);
  dataBufferOffset += static_cast<int>(len);
}

bool FrameCapture::commitData() {
  bool result = sendData(static_cast<const uint8_t*>(dataBuffer.data()) + dataBufferStart,
                         static_cast<size_t>(dataBufferOffset - dataBufferStart));
  if (dataBufferOffset > TargetFrameSize * 2) {
    dataBufferOffset = 0;
  }
  dataBufferStart = dataBufferOffset;
  return result;
}

static bool IsEncodeImage(const uint8_t* data, size_t size) {
  auto offset =
      sizeof(FrameCaptureMessageHeader) + sizeof(StringTransferMessage) + sizeof(uint32_t);
  const auto pixelsData = Data::MakeWithoutCopy(data + offset, size);
  return JpegCodec::IsJpeg(pixelsData) || WebpCodec::IsWebp(pixelsData) ||
         PngCodec::IsPng(pixelsData);
}

bool FrameCapture::sendData(const uint8_t* data, size_t len) {
  if (len == 0) {
    return true;
  }
  auto maxOutputSize = LZ4CompressionHandler::GetMaxOutputSize(len);
  if (lz4Buf.size() < maxOutputSize) {
    lz4Buf.clear();
    lz4Buf.alloc(maxOutputSize + sizeof(size_t));
    if (lz4Buf.isEmpty()) {
      LOGE("Inspector failed to send data!");
      return false;
    }
  }
  auto lz4Size = len;
  if (IsEncodeImage(data, len)) {
    memcpy(lz4Buf.bytes() + sizeof(size_t), data, len);
  } else {
    lz4Size = lz4Handler->encode(lz4Buf.bytes() + sizeof(size_t), maxOutputSize, data, len);
  }
  memcpy(lz4Buf.bytes(), &lz4Size, sizeof(size_t));
  return sock->sendData(lz4Buf.bytes(), lz4Size + sizeof(size_t)) != -1;
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

uint64_t FrameCapture::getTextureId(uint64_t texturePtr) {
  uint64_t textureId = texturePtr;
  auto texturePtrToTextureIdIter = textureIds.find(GetTextureHash(texturePtr));
  if (texturePtrToTextureIdIter != textureIds.end()) {
    textureId = texturePtrToTextureIdIter->second;
  } else {
    auto currentFrame = GetInstance().frameCount.load(std::memory_order_relaxed) + 1;
    texturePtrToTextureIdIter = textureIds.find(GetTextureHash(texturePtr, currentFrame));
    if (texturePtrToTextureIdIter != textureIds.end()) {
      textureId = texturePtrToTextureIdIter->second;
    }
  }
  return textureId;
}

void FrameCapture::handleConnect(const WelcomeMessage& welcome) {
  isConnect.store(true, std::memory_order_release);
  auto handshake = HandshakeStatus::HandshakeWelcome;
  sock->sendData(&handshake, sizeof(handshake));

  lz4Handler->reset();
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
        case FrameCaptureMessageType::TextureData: {
          auto ptr = item.textureData.pixels;
          auto csz = item.textureData.pixelsSize;
          sendPixelsData(ptr, (const char*)ptr, csz, FrameCaptureMessageType::PixelsData);
          free((void*)ptr);
          break;
        }
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
