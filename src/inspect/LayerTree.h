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
#pragma once
#include <functional>
#include <queue>
#include <thread>
#include <vector>
#include "Protocol.h"
#include "Socket.h"
#include "TCPPortProvider.h"
#include "concurrentqueue.h"

namespace tgfx::inspect {
class LayerTree {
 public:
  static LayerTree& Get() {
    static LayerTree instance;
    return instance;
  }

  void setData(const std::vector<uint8_t>& data);

  void setCallBack(std::function<void(const std::vector<uint8_t>&)> callback);

  LayerTree(const LayerTree&) = delete;

  LayerTree(LayerTree&&) = delete;

  LayerTree& operator=(const LayerTree&) = delete;

  LayerTree& operator=(LayerTree&&) = delete;

  ~LayerTree();

 protected:
  LayerTree();

  void sendWork();

  void recvWork();

  void spawnWorkTread();

 private:
#ifndef __EMSCRIPTEN__
  std::shared_ptr<ListenSocket> listenSocket = nullptr;
  std::shared_ptr<Socket> socket = nullptr;
  std::queue<std::vector<uint8_t>> messages = {};
  std::array<std::shared_ptr<UdpBroadcast>, BroadcastCount> broadcasts = {};
  bool isUDPOpened = true;
#endif
  int64_t epoch = 0;
  moodycamel::ConcurrentQueue<std::vector<uint8_t>> queue;
  std::shared_ptr<std::thread> sendThread = nullptr;
  std::shared_ptr<std::thread> recvThread = nullptr;
  std::function<void(const std::vector<uint8_t>&)> callback = nullptr;
  std::atomic<bool> stopFlag = false;
};
}  // namespace tgfx::inspect
