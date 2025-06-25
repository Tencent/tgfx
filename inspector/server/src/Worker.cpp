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
#include "tgfx/core/Data.h"

namespace inspector {
bool IsQueryPrio(ServerQuery type) {
  return type < ServerQuery::ServerQueryDisconnect;
}

Worker::Worker(const char* addr, uint16_t port)
    : addr(addr), port(port), lz4Stream(LZ4_createStreamDecode()),
      dataBuffer(new char[TargetFrameSize * 3 + 1]), bufferOffset(0) {
  workThread = std::thread([this] { Exec(); });
  netThread = std::thread([this] { Network(); });
}

Worker::Worker(std::string& filePath)
    : port(0), lz4Stream(nullptr), dataBuffer(nullptr), bufferOffset(0) {
  Open(filePath);
}

Worker::~Worker() {
  Shutdown();

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

bool Worker::Open(const std::string& filePath) {
  auto data = tgfx::Data::MakeFromFile(filePath);
  DecodeStream stream(&dataContext, data->bytes(), static_cast<uint32_t>(data->size()));
  auto body = ReadBodyBytes(&stream);
  if (dataContext.hasException()) {
    return false;
  }
  ReadTags(&body, ReadTagsOfFile);
  if (dataContext.hasException()) {
    return false;
  }

  return true;
}

bool Worker::Save(const std::string& filePath) {
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

DecodeStream Worker::ReadBodyBytes(DecodeStream* stream) {
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

int64_t Worker::GetFrameTime(const FrameData& fd, size_t idx) const {
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

int64_t Worker::GetLastTime() const {
  return dataContext.lastTime;
}

int64_t Worker::GetFrameStart(uint32_t index) const {
  return dataContext.frameData.frames[index].start;
}

int64_t Worker::GetFrameDrawCall(uint32_t index) const {
  return dataContext.frameData.frames[index].drawCall;
}

int64_t Worker::GetFrameTriangles(uint32_t index) const {
  return dataContext.frameData.frames[index].triangles;
}

FrameData* Worker::GetFrameData() {
  return &dataContext.frameData;
}

const DataContext& Worker::GetDataContext() const {
  return dataContext;
}

size_t Worker::GetFrameCount() const {
  return dataContext.frameData.frames.size();
}

bool Worker::hasExpection() const {
  return dataContext.hasException();
}

std::vector<std::string>& Worker::getErrorMessage() {
  return dataContext.errorMessages;
}

void Worker::Shutdown() {
  isShutDown.store(true, std::memory_order_relaxed);
}

#define CLOSE_EXEC                                     \
  Shutdown();                                          \
  sock.Close();                                        \
  netWriteCv.notify_one();                             \
  isConnected.store(false, std::memory_order_relaxed); \
  return;

void Worker::Exec() {
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
    WelcomeMessage welcome;
    if (!sock.Read(&welcome, sizeof(welcome), 10, ShouldExit)) {
      this->handshake.store(HandshakeDropped, std::memory_order_relaxed);
      CLOSE_EXEC;
    }
    dataContext.baseTime = welcome.initBegin;
    const auto initEnd = TscTime(welcome.initEnd);
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
      QueryTerminate();
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
        if (!DispatchProcess(*ev, ptr)) {
          QueryTerminate();
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

void Worker::Network() {
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

    auto sz = LZ4_decompress_safe_continue((LZ4_streamDecode_t*)lz4Stream, lz4buf.get(), buf, lz4sz,
                                           TargetFrameSize);
    assert(sz >= 0);
    bb = decBytes.load(std::memory_order_relaxed);
    decBytes.store(bb + static_cast<uint64_t>(sz), std::memory_order_relaxed);

    {
      std::lock_guard<std::mutex> lock(netReadLock);
      netRead.push_back(NetBuffer{bufferOffset, sz});
      netReadCv.notify_one();
    }

    bufferOffset += sz;
    if (bufferOffset > TargetFrameSize * 2) {
      bufferOffset = 0;
    }
  }
}

void Worker::NewOpTask(std::shared_ptr<OpTaskData> opTask) {
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

void Worker::Query(ServerQuery type, uint64_t data, uint32_t extra) {
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

void Worker::QueryTerminate() {
  ServerQueryPacket query{ServerQueryTerminate, 0, 0};
  sock.Send(&query, ServerQueryPacketSize);
}

bool Worker::DispatchProcess(const QueueItem& ev, const char*& ptr) {
  if (ev.hdr.idx >= static_cast<uint8_t>(QueueType::StringData)) {
    ptr += sizeof(QueueHeader) + sizeof(QueueStringTransfer);
    uint16_t sz;
    memcpy(&sz, ptr, sizeof(sz));
    ptr += sizeof(sz);
    switch (ev.hdr.type) {
      case QueueType::StringData: {
        serverQuerySpaceLeft++;
        break;
      }
      case QueueType::ValueName: {
        HandleValueName(ev.stringTransfer.ptr, ptr, sz);
        serverQuerySpaceLeft++;
        break;
      }
      default: {
        break;
      }
    }
    ptr += sz;
    return true;
  }
  ptr += QueueDataSize[ev.hdr.idx];
  return Process(ev);
}

bool Worker::Process(const QueueItem& ev) {
  switch (ev.hdr.type) {
    case QueueType::OperateBegin:
      ProcessOperateBegin(ev.operateBegin);
      break;
    case QueueType::OperateEnd:
      ProcessOperateEnd(ev.operateEnd);
      break;
    case QueueType::ValueDataUint32:
      ProcessUint32Value(ev.attributeDataUint32);
      break;
    case QueueType::ValueDataFloat4:
      ProcessFloat4Value(ev.attributeDataFloat4);
      break;
    case QueueType::ValueDataMat4:
      ProcessMat4Value(ev.attributeDataMat4);
      break;
    case QueueType::ValueDataInt:
      ProcessIntValue(ev.attributeDataInt);
      break;
    case QueueType::ValueDataColor:
      ProcessColorValue(ev.attributeDataUint32);
      break;
    case QueueType::ValueDataFloat:
      ProcessFloatValue(ev.attributeDataFloat);
      break;
    case QueueType::ValueDataBool:
      ProcessBoolValue(ev.attributeDataBool);
      break;
    case QueueType::ValueDataEnum:
      ProcessEnumValue(ev.attributeDataEnum);
      break;
    case QueueType::FrameMarkMsg:
      ProcessFrameMark(ev.frameMark);
      break;
    case QueueType::KeepAlive:
      break;
    default:
      break;
  }
  return true;
}

int64_t RefTime(int64_t& reference, int64_t delta) {
  const auto refTime = reference + delta;
  reference = refTime;
  return refTime;
}

void Worker::ProcessOperateBegin(const QueueOperateBegin& ev) {
  std::shared_ptr<OpTaskData> opTask(new OpTaskData);
  const auto start = TscTime(RefTime(refTime, ev.time));
  opTask->start = start;
  opTask->end = -1;
  opTask->type = ev.type;

  NewOpTask(std::move(opTask));
}

void Worker::ProcessOperateEnd(const QueueOperateEnd& ev) {
  auto& stack = dataContext.opTaskStack;
  if (stack.empty()) {
    return;
  }
  auto opTask = stack.back();
  stack.pop_back();
  assert(opTask->end == -1);
  assert(opTask->type == ev.type);
  const auto timeEnd = TscTime(RefTime(refTime, ev.time));
  opTask->end = timeEnd;
  assert(timeEnd >= opTask->start);
}

void Worker::ProcessAttributeImpl(DataHead& head, std::shared_ptr<tgfx::Data> data) {
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
    Query(ServerQueryValueName, head.name);
  }
  propertyData->summaryName.push_back(head);
  auto& summaryData = propertyData->summaryData;
  summaryData.emplace_back(data);
  dataContext.properties[opTask->id] = propertyData;
}

void Worker::ProcessFloatValue(const QueueAttributeDataFloat& ev) {
  auto head = DataHead{DataType::Float, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(float));
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessFloat4Value(const QueueAttributeDataFloat4& ev) {
  auto head = DataHead{DataType::Vec4, ev.name};
  auto data = tgfx::Data::MakeWithCopy(ev.value, sizeof(float) * 4);
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessIntValue(const QueueAttributeDataInt& ev) {
  auto head = DataHead{DataType::Int, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(int));
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessBoolValue(const QueueAttributeDataBool& ev) {
  auto head = DataHead{DataType::Bool, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(bool));
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessMat4Value(const QueueAttributeDataMat4& ev) {
  auto head = DataHead{DataType::Mat4, ev.name};
  auto data = tgfx::Data::MakeWithCopy(ev.value, sizeof(float) * 6);
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessEnumValue(const QueueAttributeDataEnum& ev) {
  auto head = DataHead{DataType::Enum, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(uint16_t));
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessUint32Value(const QueueAttributeDataUInt32& ev) {
  auto head = DataHead{DataType::Uint32, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(uint32_t));
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessColorValue(const QueueAttributeDataUInt32& ev) {
  auto head = DataHead{DataType::Color, ev.name};
  auto data = tgfx::Data::MakeWithCopy(&ev.value, sizeof(uint32_t));
  ProcessAttributeImpl(head, std::move(data));
}

void Worker::ProcessFrameMark(const QueueFrameMark& ev) {
  auto& fd = dataContext.frameData;

  const auto time = TscTime(ev.time);
  fd.frames.push_back(FrameEvent{time, -1, 0, 0, -1});
  if (dataContext.lastTime < time) {
    dataContext.lastTime = time;
  }
}

void Worker::HandleValueName(uint64_t name, const char* str, size_t sz) {
  auto& nameMap = dataContext.nameMap;
  if (nameMap.find(name) == nameMap.end()) {
    nameMap[name] = std::string(str, sz);
  }
}

}  // namespace inspector
