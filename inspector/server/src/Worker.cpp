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

#include "Worker.h"
#include <tgfx/core/WriteStream.h>
#include <cassert>
#include <iostream>
#include "DecodeStream.h"
#include "EncodeStream.h"
#include "FileTags.h"
#include "LZ4.h"
#include "Protocol.h"
#include "Queue.h"
#include "TagHeader.h"
#include "Utils.h"
#include "tgfx/core/Data.h"

namespace inspector {

tgfx::ColorType PixelFormatToColorType(tgfx::PixelFormat format) {
  switch (format) {
    case tgfx::PixelFormat::RGBA_8888:
      return tgfx::ColorType::RGBA_8888;
    case tgfx::PixelFormat::ALPHA_8:
      return tgfx::ColorType::ALPHA_8;
    case tgfx::PixelFormat::BGRA_8888:
      return tgfx::ColorType::BGRA_8888;
    case tgfx::PixelFormat::GRAY_8:
      return tgfx::ColorType::Gray_8;
    default:
      return tgfx::ColorType::Unknown;
  }
}

bool IsQueryPrio(ServerQuery type) {
  return type < ServerQuery::ServerQueryDisconnect;
}

Worker::Worker(const char* addr, uint16_t port)
    : addr(addr), port(port), lz4Stream(LZ4_createStreamDecode()),
      dataBuffer(new char[MaxTargetSize]), bufferOffset(0) {
  workThread = std::thread([this] { exec(); });
  netThread = std::thread([this] { netWork(); });
}

Worker::Worker(const std::string& filePath)
    : port(0), lz4Stream(nullptr), dataBuffer(nullptr), bufferOffset(0) {
  openFile(filePath);
}

Worker::~Worker() {
  shutdown();

  if (netThread.joinable()) {
    netThread.join();
  }
  if (workThread.joinable()) {
    workThread.join();
  }
  if (dataBuffer) {
    delete[] dataBuffer;
  }
  if (lz4Stream) {
    LZ4_freeStreamDecode((LZ4_streamDecode_t*)lz4Stream);
  }
}

bool Worker::openFile(const std::string& filePath) {
  auto data = tgfx::Data::MakeFromFile(filePath);
  DecodeStream stream(&dataContext, data->bytes(), static_cast<uint32_t>(data->size()));
  auto body = readBodyBytes(&stream);
  if (dataContext.hasException()) {
    return false;
  }
  ReadTags(&body, ReadTagsOfFile);
  if (dataContext.hasException()) {
    return false;
  }
  return true;
}

bool Worker::saveFile(const std::string& filePath) {
  EncodeStream bodyBytes(&dataContext);
  WriteTagsOfFile(&bodyBytes);

  EncodeStream fileBytes(&dataContext);
  fileBytes.writeInt8('T');
  fileBytes.writeInt8('G');
  fileBytes.writeInt8('F');
  fileBytes.writeInt8('X');
  fileBytes.writeUint8(ProtocolVersion);
  fileBytes.writeEncodedUint32(bodyBytes.length());
  fileBytes.writeBytes(&bodyBytes);
  auto data = fileBytes.release();
  auto writeStream = tgfx::WriteStream::MakeFromFile(filePath);
  if (!writeStream) {
    return false;
  }
  writeStream->write(data->bytes(), data->size());
  return true;
}

DecodeStream Worker::readBodyBytes(DecodeStream* stream) {
  DecodeStream emptyStream(stream->context);
  auto T = stream->readInt8();
  auto G = stream->readInt8();
  auto F = stream->readInt8();
  auto X = stream->readInt8();
  if (T != 'T' || G != 'G' || F != 'F' || X != 'X') {
    InspectorThrowError(stream->context, "Invalid ISP file header");
    return emptyStream;
  }

  auto version = stream->readUint8();
  if (version > ProtocolVersion) {
    InspectorThrowError(stream->context, "Isp file version is too high");
    return emptyStream;
  }
  auto bodyLength = stream->readEncodedUint32();
  bodyLength = std::min(bodyLength, stream->bytesAvailable());
  return stream->readBytes(bodyLength);
}

int64_t Worker::getFrameTime(const FrameData& fd, size_t idx) const {
  if (fd.continuous) {
    if (idx < fd.frames.size() - 1) {
      return fd.frames[idx + 1].start - fd.frames[idx].start;
    }
    assert(dataContext.lastTime != 0);
    return dataContext.lastTime - fd.frames.back().start;
  }
  const auto& frame = fd.frames[idx];
  if (frame.end >= 0) {
    return frame.end - frame.start;
  }
  return dataContext.lastTime - fd.frames.back().start;
}

int64_t Worker::getLastTime() const {
  return dataContext.lastTime;
}

int64_t Worker::getFrameStart(uint32_t index) const {
  return dataContext.frameData.frames[index].start;
}

int64_t Worker::getFrameDrawCall(uint32_t index) const {
  return dataContext.frameData.frames[index].drawCall;
}

int64_t Worker::getFrameTriangles(uint32_t index) const {
  return dataContext.frameData.frames[index].triangles;
}

FrameData* Worker::getFrameData() {
  return &dataContext.frameData;
}

const DataContext& Worker::getDataContext() const {
  return dataContext;
}

size_t Worker::getFrameCount() const {
  return dataContext.frameData.frames.size();
}

bool Worker::hasExpection() const {
  return dataContext.hasException();
}

std::vector<std::string>& Worker::getErrorMessage() {
  return dataContext.errorMessages;
}

void Worker::shutdown() {
  isShutDown.store(true, std::memory_order_relaxed);
}

#define CLOSE_EXEC                                     \
  shutdown();                                          \
  sock.Close();                                        \
  netWriteCv.notify_one();                             \
  isConnected.store(false, std::memory_order_relaxed); \
  return;

void Worker::exec() {
  auto ShouldExit = [this] { return isShutDown.load(std::memory_order_relaxed); };

  while (true) {
    if (isShutDown.load(std::memory_order_relaxed)) {
      return;
    }
    if (sock.Connect(addr.c_str(), port)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  sock.Send(HandshakeShibboleth, HandshakeShibbolethSize);
  uint32_t protocolVersion = ProtocolVersion;
  sock.Send(&protocolVersion, sizeof(protocolVersion));
  HandshakeStatus handshake;
  if (!sock.Read(&handshake, sizeof(handshake), 10, ShouldExit)) {
    this->handshake.store(HandshakeDropped, std::memory_order_relaxed);
    CLOSE_EXEC;
  }
  this->handshake.store(handshake, std::memory_order_relaxed);
  switch (handshake) {
    case HandshakeWelcome:
      break;
    case HandshakeProtocolMismatch:
    case HandshakeNotAvailable:
    default:
      CLOSE_EXEC;
  }

  {
    WelcomeMessage welcome = {};
    if (!sock.Read(&welcome, sizeof(welcome), 10, ShouldExit)) {
      this->handshake.store(HandshakeDropped, std::memory_order_relaxed);
      CLOSE_EXEC;
    }
    dataContext.baseTime = welcome.initBegin;
    const auto initEnd = tscTime(welcome.initEnd);
    dataContext.frameData.frames.push_back(FrameEvent{0, -1, 0, 0, -1});
    dataContext.frameData.frames.push_back(FrameEvent{initEnd, -1, 0, 0, -1});
    dataContext.lastTime = initEnd;
    refTime = welcome.refTime;
  }
  // leave space for terminate request
  serverQuerySpaceLeft = serverQuerySpaceBase =
      size_t(std::min(sock.GetSendBufSize() / ServerQueryPacketSize, 8 * 1024) - 4);
  hasData.store(true, std::memory_order_release);

  LZ4_setStreamDecode((LZ4_streamDecode_t*)lz4Stream, nullptr, 0);
  isConnected.store(true, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(netWriteLock);
    netWriteCnt = 2;
    netWriteCv.notify_one();
  }

  while (true) {
    if (isShutDown.load(std::memory_order_relaxed)) {
      queryTerminate();
      CLOSE_EXEC;
    }

    NetBuffer netbuf;
    {
      std::unique_lock<std::mutex> lock(netReadLock);
      netReadCv.wait(lock, [this] { return !netRead.empty(); });
      netbuf = netRead.front();
      netRead.erase(netRead.begin());
    }

    if (netbuf.bufferOffset < 0) {
      CLOSE_EXEC;
    }

    const char* ptr = dataBuffer + netbuf.bufferOffset;
    const char* end = ptr + netbuf.size;

    {
      std::lock_guard<std::mutex> lock(dataContext.lock);
      while (ptr < end) {
        auto ev = (const QueueItem*)ptr;
        if (!dispatchProcess(*ev, ptr)) {
          queryTerminate();
          CLOSE_EXEC;
        }
      }

      {
        std::lock_guard<std::mutex> lockNet(netWriteLock);
        ++netWriteCnt;
        netWriteCv.notify_one();
      }

      if (serverQuerySpaceLeft > 0 && !serverQueryQueuePrio.empty()) {
        const auto toSend = std::min(serverQuerySpaceLeft, serverQueryQueuePrio.size());
        sock.Send(serverQueryQueuePrio.data(), toSend * ServerQueryPacketSize);
        serverQuerySpaceLeft -= toSend;
        if (toSend == serverQueryQueuePrio.size()) {
          serverQueryQueuePrio.clear();
        } else {
          serverQueryQueuePrio.erase(serverQueryQueuePrio.begin(),
                                     serverQueryQueuePrio.begin() + long(toSend));
        }
      }
      if (serverQuerySpaceLeft > 0 && !serverQueryQueue.empty()) {
        const auto toSend = std::min(serverQuerySpaceLeft, serverQueryQueue.size());
        sock.Send(serverQueryQueue.data(), toSend * ServerQueryPacketSize);
        serverQuerySpaceLeft -= toSend;
        if (toSend == serverQueryQueue.size()) {
          serverQueryQueue.clear();
        } else {
          serverQueryQueue.erase(serverQueryQueue.begin(), serverQueryQueue.begin() + long(toSend));
        }
      }
    }
  }
}

#define CLOSE_NETWORK                            \
  std::lock_guard<std::mutex> lock(netReadLock); \
  netRead.push_back(NetBuffer{-1, 0});           \
  netReadCv.notify_one();                        \
  return;

void Worker::netWork() {
  auto ShouldExit = [this] { return isShutDown.load(std::memory_order_relaxed); };

  auto lz4buf = std::unique_ptr<char[]>(new char[LZ4Size]);
  while (true) {
    {
      std::unique_lock<std::mutex> lock(netWriteLock);
      netWriteCv.wait(
          lock, [this] { return netWriteCnt > 0 || isShutDown.load(std::memory_order_relaxed); });
      if (isShutDown.load(std::memory_order_relaxed)) {
        CLOSE_NETWORK;
      }
      netWriteCnt--;
    }

    auto buf = dataBuffer + bufferOffset;
    lz4sz_t lz4sz;
    if (!sock.Read(&lz4sz, sizeof(lz4sz), 10, ShouldExit)) {
      CLOSE_NETWORK;
    }
    if (!sock.Read(lz4buf.get(), static_cast<size_t>(lz4sz), 10, ShouldExit)) {
      CLOSE_NETWORK;
    }
    auto bb = bytes.load(std::memory_order_relaxed);
    bytes.store(bb + sizeof(lz4sz) + static_cast<size_t>(lz4sz), std::memory_order_relaxed);

    auto sz = 0;
    if (lz4sz <= TargetFrameSize) {
      sz = LZ4_decompress_safe_continue((LZ4_streamDecode_t*)lz4Stream, lz4buf.get(), buf, lz4sz,
                                             TargetFrameSize);
    }
    else {
      sz = lz4sz;
      // MemWrite(buf, lz4buf.get(), static_cast<size_t>(lz4sz));
    }
    assert(sz >= 0);
    bb = decBytes.load(std::memory_order_relaxed);
    decBytes.store(bb + static_cast<uint64_t>(sz), std::memory_order_relaxed);

    {
      std::lock_guard<std::mutex> lock(netReadLock);
      netRead.push_back(NetBuffer{bufferOffset, sz});
      netReadCv.notify_one();
    }

    bufferOffset += sz;
    if (bufferOffset > MaxTargetSize) {
      bufferOffset = 0;
    }
  }
}

void Worker::newOpTask(const std::shared_ptr<OpTaskData>& opTask) {
  ++dataContext.opTaskCount;

  auto& stack = dataContext.opTaskStack;
  const auto size = stack.size();
  opTask->id = static_cast<uint32_t>(dataContext.opTasks.size());
  dataContext.opTasks.push_back(opTask);
  if (size != 0) {
    auto& back = stack.back();
    if (dataContext.opChilds.find(back->id) == dataContext.opChilds.end()) {
      dataContext.opChilds[back->id] = std::vector<uint32_t>{opTask->id};
    } else {
      dataContext.opChilds[back->id].push_back(opTask->id);
    }
  }
  stack.push_back(opTask);
}

void Worker::query(ServerQuery type, uint64_t data, uint32_t extra) {
  ServerQueryPacket query{type, data, extra};
  if (serverQuerySpaceLeft > 0 && serverQueryQueuePrio.empty() && serverQueryQueue.empty()) {
    serverQuerySpaceLeft--;
    sock.Send(&query, ServerQueryPacketSize);
  } else if (IsQueryPrio(type)) {
    serverQueryQueuePrio.push_back(query);
  } else {
    serverQueryQueue.push_back(query);
  }
}

void Worker::queryTerminate() {
  ServerQueryPacket query{ServerQueryTerminate, 0, 0};
  sock.Send(&query, ServerQueryPacketSize);
}

bool Worker::dispatchProcess(const QueueItem& ev, const char*& ptr) {
  if (ev.hdr.idx >= static_cast<uint8_t>(QueueType::StringData)) {
    ptr += sizeof(QueueHeader) + sizeof(QueueStringTransfer);
    if (ev.hdr.type == QueueType::PixelsData) {
      uint32_t sz;
      memcpy(&sz, ptr, sizeof(sz));
      ptr += sizeof(sz);

    }
    else {
      uint16_t sz;
      memcpy(&sz, ptr, sizeof(sz));
      ptr += sizeof(sz);
      switch (ev.hdr.type) {
        case QueueType::StringData: {
          serverQuerySpaceLeft++;
          break;
        }
        case QueueType::ValueName: {
          handleValueName(ev.stringTransfer.ptr, ptr, sz);
          serverQuerySpaceLeft++;
          break;
        }
        case QueueType::PixelsData: {
          handleTexturePixels(ev.stringTransfer.ptr, ptr, sz);
          break;
        }
        default: {
          break;
        }
      }
      ptr += sz;
      return true;
    }
  }
  ptr += QueueDataSize[ev.hdr.idx];
  return process(ev);
}

bool Worker::process(const QueueItem& ev) {
  switch (ev.hdr.type) {
    case QueueType::OperateBegin:
      processOperateBegin(ev.operateBegin);
      break;
    case QueueType::OperateEnd:
      processOperateEnd(ev.operateEnd);
      break;
    case QueueType::ValueDataUint32:
      processUint32Value(ev.attributeDataUint32);
      break;
    case QueueType::ValueDataFloat4:
      processFloat4Value(ev.attributeDataFloat4);
      break;
    case QueueType::ValueDataMat4:
      processMat4Value(ev.attributeDataMat4);
      break;
    case QueueType::ValueDataInt:
      processIntValue(ev.attributeDataInt);
      break;
    case QueueType::ValueDataColor:
      processColorValue(ev.attributeDataUint32);
      break;
    case QueueType::ValueDataFloat:
      processFloatValue(ev.attributeDataFloat);
      break;
    case QueueType::ValueDataBool:
      processBoolValue(ev.attributeDataBool);
      break;
    case QueueType::ValueDataEnum:
      processEnumValue(ev.attributeDataEnum);
      break;
    case QueueType::FrameMarkMsg:
      processFrameMark(ev.frameMark);
      break;
    case QueueType::TextureData:
      processTextureData(ev.textureData);
      break;
    case QueueType::TextureSampler:
      processTextureSampler(ev.textureSampler);
      break;
    case QueueType::KeepAlive:
      break;
    default:
      return false;
  }
  return true;
}

int64_t RefTime(int64_t& reference, int64_t delta) {
  const auto refTime = reference + delta;
  reference = refTime;
  return refTime;
}

void Worker::processOperateBegin(const QueueOperateBegin& ev) {
  std::shared_ptr<OpTaskData> opTask(new OpTaskData);
  const auto start = tscTime(RefTime(refTime, ev.nsTime));
  opTask->start = start;
  opTask->end = -1;
  opTask->type = ev.type;

  newOpTask(std::move(opTask));
}

void Worker::processOperateEnd(const QueueOperateEnd& ev) {
  auto& stack = dataContext.opTaskStack;
  if (stack.empty()) {
    return;
  }
  auto opTask = stack.back();
  stack.pop_back();
  assert(opTask->end == -1);
  assert(opTask->type == ev.type);
  const auto timeEnd = tscTime(RefTime(refTime, ev.nsTime));
  opTask->end = timeEnd;
  assert(timeEnd >= opTask->start);
}

void Worker::processAttributeImpl(DataHead& head, std::shared_ptr<tgfx::Data> data) {
  auto& stack = dataContext.opTaskStack;
  if (stack.empty()) {
    return;
  }
  auto opTask = stack.back();
  auto& nameMap = dataContext.nameMap;
  auto propertyIter = dataContext.properties.find(opTask->id);
  std::shared_ptr<PropertyData> propertyData;
  if (propertyIter == dataContext.properties.end()) {
    propertyData = std::make_shared<PropertyData>();
  } else {
    propertyData = propertyIter->second;
  }
  if (nameMap.find(head.name) == nameMap.end()) {
    query(ServerQueryValueName, head.name);
  }
  propertyData->summaryName.push_back(head);
  auto& summaryData = propertyData->summaryData;
  summaryData.push_back(std::move(data));
  dataContext.properties[opTask->id] = propertyData;
}

void Worker::processFloatValue(const QueueAttributeDataFloat& ev) {
  auto head = DataHead{DataType::Float, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(float));
  processAttributeImpl(head, std::move(data));
}

void Worker::processFloat4Value(const QueueAttributeDataFloat4& ev) {
  auto head = DataHead{DataType::Vec4, ev.name};
  auto data = tgfx::Data::MakeWithCopy(ev.value, sizeof(float) * 4);
  processAttributeImpl(head, std::move(data));
}

void Worker::processIntValue(const QueueAttributeDataInt& ev) {
  auto head = DataHead{DataType::Int, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(int));
  processAttributeImpl(head, std::move(data));
}

void Worker::processBoolValue(const QueueAttributeDataBool& ev) {
  auto head = DataHead{DataType::Bool, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(bool));
  processAttributeImpl(head, std::move(data));
}

void Worker::processMat4Value(const QueueAttributeDataMat4& ev) {
  auto head = DataHead{DataType::Mat4, ev.name};
  auto data = tgfx::Data::MakeWithCopy(ev.value, sizeof(float) * 6);
  processAttributeImpl(head, std::move(data));
}

void Worker::processEnumValue(const QueueAttributeDataEnum& ev) {
  auto head = DataHead{DataType::Enum, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(uint16_t));
  processAttributeImpl(head, std::move(data));
}

void Worker::processUint32Value(const QueueAttributeDataUInt32& ev) {
  auto head = DataHead{DataType::Uint32, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(uint32_t));
  processAttributeImpl(head, std::move(data));
}

void Worker::processColorValue(const QueueAttributeDataUInt32& ev) {
  auto head = DataHead{DataType::Color, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(uint32_t));
  processAttributeImpl(head, std::move(data));
}

void Worker::processFrameMark(const QueueFrameMark& ev) {
  auto& fd = dataContext.frameData;

  const auto time = tscTime(ev.nsTime);
  fd.frames.push_back(FrameEvent{time, -1, 0, 0, -1});
  if (dataContext.lastTime < time) {
    dataContext.lastTime = time;
  }
}

void Worker::processTextureData(const QueueTextureData& ev) {
  auto& stack = dataContext.opTaskStack;
  if (stack.empty()) {
    return;
  }
  auto opTask = stack.back();
  auto& textures = dataContext.textures;

  auto texture = textures.find(opTask->id);
  std::shared_ptr<TextureData> textureData;
  if (texture == textures.end()) {
    textureData = std::make_shared<TextureData>();
    textures[opTask->id] = textureData;
  }
  else {
    textureData = texture->second;
  }
  auto input = textureData->inputTexture;
  auto imageInfo = tgfx::ImageInfo::Make(ev.width, ev.height, PixelFormatToColorType(static_cast<tgfx::PixelFormat>(ev.format)));
  auto& pixelsData = dataContext.pixelsData;
  auto pixelsIter = pixelsData.find(ev.samplerPtr);
  if (pixelsIter == pixelsData.end()) {
    return;
  }
  auto pixels = pixelsIter->second;
  auto pixmap = std::make_shared<tgfx::Pixmap>(imageInfo, pixels->data());
  dataContext.pixelmap[ev.samplerPtr] = pixmap;
  pixelsData.erase(ev.samplerPtr);
  input.push_back(pixmap);
}

void Worker::processTextureSampler(const QueueTextureSampler& ev){
  auto& stack = dataContext.opTaskStack;
  if (stack.empty()) {
    return;
  }
  auto opTask = stack.back();
  auto& textures = dataContext.textures;
  auto texture = textures.find(opTask->id);
  std::shared_ptr<TextureData> textureData;
  if (texture == textures.end()) {
    textureData = std::make_shared<TextureData>();
    textures[opTask->id] = textureData;
  }
  else {
    textureData = texture->second;
  }
  auto& pixelmap = dataContext.pixelmap;
  auto pixmapIter = pixelmap.find(ev.samplerPtr);
  if (pixmapIter == pixelmap.end()) {
    return;
  }
  auto pixmap = pixmapIter->second;
  auto input = textureData->inputTexture;
  input.push_back(pixmap);
}

void Worker::addPixelsData(const char *data, size_t sz) {
  ASSERT(sz % 8 == 0);
  if (pixelsDataSize < sz) {
    pixelsDataSize = sz;
    delete[] pixelsDataBuffer;
    pixelsDataBuffer = new char[sz];
  }

  auto src = (uint8_t*)data;
  auto dst = (uint8_t*)pixelsDataBuffer;
  memcpy(dst, src, sz);

}


void Worker::handleValueName(uint64_t name, const char* str, size_t sz) {
  auto& nameMap = dataContext.nameMap;
  if (nameMap.find(name) == nameMap.end()) {
    nameMap[name] = std::string(str, sz);
  }
}

void Worker::handleTexturePixels(uint64_t samplerPtr, const void* data, size_t sz) {
  auto& pixlesData = dataContext.pixelsData;
  if (pixlesData.find(samplerPtr) == pixlesData.end()) {
    pixlesData[samplerPtr] = tgfx::Data::MakeWithCopy(data, sz);
  }
}
}  // namespace inspector
