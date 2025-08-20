/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "LayerProfiler.h"
#include <chrono>
#include <cstring>
#include <thread>
#include "ProcessUtils.h"
#include "Protocol.h"
#include "tgfx/core/Clock.h"

namespace tgfx::debug {

#ifndef __EMSCRIPTEN__
static const char* addr = "255.255.255.255";
static uint16_t broadcastPort = 8086;
#endif

LayerProfiler::LayerProfiler() {
#ifndef __EMSCRIPTEN__
  listenSocket = std::make_shared<ListenSocket>();
  for (uint16_t i = 0; i < BroadcastNum; i++) {
    broadcasts[i] = std::make_shared<UdpBroadcast>();
    isUDPOpened = isUDPOpened && broadcasts[i]->openConnect(addr, broadcastPort + i);
  }

#endif
  epoch = Clock::Now();
  spawnWorkTread();
}

LayerProfiler::~LayerProfiler() {
  stopFlag.store(true, std::memory_order_release);
  if (sendThread && sendThread->joinable()) {
    sendThread->join();
  }
  if (recvThread && recvThread->joinable()) {
    recvThread->join();
  }
}

void LayerProfiler::sendWork() {
#ifndef __EMSCRIPTEN__
  if (!isUDPOpened) {
    return;
  }
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);
  auto port = TCPPortProvider::Get()->getValidPort();
  if (port == 0) {
    return;
  }
  if (!listenSocket->listenSock(port, 4)) {
    return;
  }
  size_t broadcastLen = 0;
  auto broadcastMsg = GetBroadcastMessage(procname, pnsz, broadcastLen, port, ToolType::LayerTree);
  long long lastBroadcast = 0;
  while (!stopFlag.load(std::memory_order_acquire)) {
    while (!stopFlag.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      if (t - lastBroadcast > 3000000000) {
        lastBroadcast = t;
        for (uint16_t i = 0; i < BroadcastNum; i++) {
          if (broadcasts[i]) {
            const auto ts = Clock::Now();
            broadcastMsg.activeTime = static_cast<int32_t>(ts - epoch);
            broadcasts[i]->sendData(broadcastPort + i, &broadcastMsg, broadcastLen);
          }
        }
      }
      socket = listenSocket->acceptSock();
      if (socket) {
        break;
      }
    }

    while (!stopFlag.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (!socket) {
        break;
      }
      if (queue.size_approx() != 0) {
        std::vector<uint8_t> data;
        if (queue.try_dequeue(data)) {
          int size = (int)data.size();
          socket->sendData(&size, sizeof(int));
          socket->sendData(data.data(), data.size());
        }
      }
    }
  }
#endif
}

void LayerProfiler::recvWork() {
#ifndef __EMSCRIPTEN__
  while (!stopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (socket && socket->hasData()) {
      int size;
      int flag = socket->readUpTo(&size, sizeof(int));
      std::vector<uint8_t> data(static_cast<size_t>(size));
      int flag1 = socket->readUpTo(data.data(), (size_t)size);
      messages.push(std::move(data));
      if (!flag || !flag1) {
        socket.reset();
      }
    }
    if (!messages.empty()) {
      if (callback) {
        callback(messages.front());
        messages.pop();
      }
    }
  }
#endif
}

void LayerProfiler::spawnWorkTread() {
  stopFlag.store(false, std::memory_order_release);
  sendThread = std::make_shared<std::thread>(&LayerProfiler::sendWork, this);
  recvThread = std::make_shared<std::thread>(&LayerProfiler::recvWork, this);
}

void LayerProfiler::setData(const std::vector<uint8_t>& data) {
  queue.enqueue(data);
}
void LayerProfiler::setCallBack(std::function<void(const std::vector<uint8_t>&)> callback) {
  this->callback = std::move(callback);
}
}  // namespace tgfx::debug
