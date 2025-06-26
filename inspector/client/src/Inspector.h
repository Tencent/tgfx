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
#include <iostream>
#include <mutex>
#include <thread>
#include "FastVector.h"
#include "Protocol.h"
#include "Queue.h"
#include "Singleton.h"
#include "Socket.h"
#include "Utils.h"

#if defined _WIN32
#  include <intrin.h>
#endif
#ifdef __APPLE__
#  include <TargetConditionals.h>
#  include <mach/mach_time.h>
#endif

#if ( (defined _WIN32 && !(defined _M_ARM64 || defined _M_ARM)) || ( defined __i386 || defined _M_IX86 || defined __x86_64__ || defined _M_X64 ) || ( defined TARGET_OS_IOS && TARGET_OS_IOS == 1 ) )
#  define INSPECTOR_HW_TIMER
#endif

#if defined INSPECTOR_HW_TIMER
inline bool HardwareSupportsInvariantTSC()
{
  return true;  // this is checked at startup
}
#else
bool HardwareSupportsInvariantTSC()
{
  return false;
}
#endif

namespace inspector {
#define QueuePrepare(_type)             \
  auto item = Inspector::QueueSerial(); \
  MemWrite(&item->hdr.type, _type);
#define QueueCommit(name) Inspector::QueueSerialFinish();

class Inspector;
Inspector& GetInspector();
uint32_t GetThreadHandle();

class Inspector {
 public:
  Inspector();
  ~Inspector();

  static int64_t GetTime() {
#ifdef INSPECTOR_HW_TIMER
#  if defined TARGET_OS_IOS && TARGET_OS_IOS == 1
        if( HardwareSupportsInvariantTSC() ) {
          return mach_absolute_time();
        }
#  elif defined _WIN32
#    ifdef INSPECTOR_TIMER_QPC
        return GetTimeQpc();
#    else
        if(HardwareSupportsInvariantTSC()) {
          return int64_t(__rdtsc());
        }
#    endif
#  elif defined __i386 || defined _M_IX86
        if(HardwareSupportsInvariantTSC())
        {
            uint32_t eax, edx;
            asm volatile ( "rdtsc" : "=a" (eax), "=d" (edx) );
            return ( uint64_t( edx ) << 32 ) + uint64_t( eax );
        }
#  elif defined __x86_64__ || defined _M_X64
        if( HardwareSupportsInvariantTSC() )
        {
            uint64_t rax, rdx;
            asm volatile ( "rdtsc" : "=a" (rax), "=d" (rdx) );
            return (int64_t)(( rdx << 32 ) + rax);
        }
#  else
#    error "INSPECTOR_HW_TIMER detection logic needs fixing"
#  endif
#endif

#if !defined INSPECTOR_HW_TIMER
#  if defined __linux__ && defined CLOCK_MONOTONIC_RAW
        struct timespec ts;
        clock_gettime( CLOCK_MONOTONIC_RAW, &ts );
        return int64_t( ts.tv_sec ) * 1000000000ll + int64_t( ts.tv_nsec );
#  else
        return std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::high_resolution_clock::now().time_since_epoch() ).count();
#  endif
#endif

#if !defined INSPECTOR_TIMER_FALLBACK
        return 0;  // unreachable branch
#endif
  }

  static QueueItem* QueueSerial() {
    auto& inspector = GetInspector();
    inspector.serialLock.lock();
    return inspector.serialQueue.prepare_next();
  }

  static void QueueSerialFinish() {
    auto& inspector = GetInspector();
    inspector.serialQueue.commit_next();
    inspector.serialLock.unlock();
  }

  static void SendFrameMark(const char* name) {
    if (!name) {
      GetInspector().frameCount.fetch_add(1, std::memory_order_relaxed);
    }
    auto item = QueueSerial();
    MemWrite(&item->hdr.type, QueueType::FrameMarkMsg);
    MemWrite(&item->frameMark.time, GetTime());
    QueueSerialFinish();
  }

  static void SendAttributeData(const char* name, int val) {
    QueuePrepare(QueueType::ValueDataInt);
    MemWrite(&item->attributeDataInt.name, (uint64_t)name);
    MemWrite(&item->attributeDataInt.value, val);
    QueueCommit(attributeDataInt);
  }

  static void SendAttributeData(const char* name, float val) {
    QueuePrepare(QueueType::ValueDataFloat)
        MemWrite(&item->attributeDataFloat.name, (uint64_t)name);
    MemWrite(&item->attributeDataFloat.value, val);
    QueueCommit(attributeDataFloat);
  }

  static void SendAttributeData(const char* name, bool val) {
    QueuePrepare(QueueType::ValueDataBool);
    MemWrite(&item->attributeDataBool.name, (uint64_t)name);
    MemWrite(&item->attributeDataBool.value, val);
    QueueCommit(attributeDataBool);
  }

  static void SendAttributeData(const char* name, uint8_t val, uint8_t type) {
    QueuePrepare(QueueType::ValueDataEnum);
    MemWrite(&item->attributeDataEnum.name, (uint64_t)name);
    auto value = static_cast<uint16_t>(type << 8 | val);
    MemWrite(&item->attributeDataEnum.value, value);
    QueueCommit(attributeDataEnum);
  }

  static void SendAttributeData(const char* name, uint32_t val,
                                QueueType type = QueueType::ValueDataUint32) {
    QueuePrepare(type);
    MemWrite(&item->attributeDataUint32.name, (uint64_t)name);
    MemWrite(&item->attributeDataUint32.value, val);
    QueueCommit(attributeDataUint32);
  }

  static void SendAttributeData(const char* name, float* val, int size) {
    if (size == 4) {
      QueuePrepare(QueueType::ValueDataFloat4);
      MemWrite(&item->attributeDataFloat4.name, (uint64_t)name);
      MemWrite(item->attributeDataFloat4.value, val, static_cast<size_t>(size) * sizeof(float));
      QueueCommit(attributeDataFloat4);
    } else if (size == 6) {
      QueuePrepare(QueueType::ValueDataMat4);
      MemWrite(&item->attributeDataMat4.name, (uint64_t)name);
      MemWrite(item->attributeDataMat4.value, val, static_cast<size_t>(size) * sizeof(float));
      QueueCommit(attributeDataMat4);
    }
  }

  static void LaunchWorker(Inspector* inspector) {
    inspector->Worker();
  }

  static bool ShouldExit();

  void SendString(uint64_t str, const char* ptr, QueueType type) {
    SendString(str, ptr, strlen(ptr), type);
  }
  void SendString(uint64_t str, const char* ptr, size_t len, QueueType type);

  void Worker();
  void SpawnWorkerThreads();
  bool HandleServerQuery();

 private:
  enum class DequeueStatus { DataDequeued, ConnectionLost, QueueEmpty };
  enum class ThreadCtxStatus { Same, Changed };

  bool AppendData(const void* data, size_t len) {
    const auto ret = NeetDataSize(len);
    AppendDataUnsafe(data, len);
    return ret;
  }

  bool NeetDataSize(size_t len) {
    assert(len <= TargetFrameSize);
    bool ret = true;
    if (size_t(dataBufferOffset - dataBufferStart) + len > TargetFrameSize) {
      ret = CommitData();
    }
    return ret;
  }

  void AppendDataUnsafe(const void* data, size_t len) {
    memcpy(dataBuffer + dataBufferOffset, data, len);
    dataBufferOffset += int(len);
  }

  bool CommitData();
  bool SendData(const char* data, size_t len);
  DequeueStatus DequeueSerial();

 private:
  int64_t epoch;
  char* dataBuffer = nullptr;
  char* lz4Buf = nullptr;
  std::atomic<bool> shutdown;
  std::atomic<int64_t> timeBegin;
  std::atomic<uint64_t> frameCount;
  std::atomic<bool> isConnect;
  Socket* sock = nullptr;

  int64_t refTimeThread;

  FastVector<QueueItem> serialQueue;
  FastVector<QueueItem> serialDequeue;
  std::mutex serialLock;

  std::thread* messageThread = nullptr;
  UdpBroadcast* broadcast[broadcastNum] = {nullptr};
  const char* programName = nullptr;
  std::mutex programNameLock;

  void* lz4Stream = nullptr;
  int dataBufferOffset;
  int dataBufferStart;
};
}  // namespace inspector