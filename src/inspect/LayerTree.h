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
#include <unordered_map>
#include <vector>
#include "Protocol.h"
#include "Socket.h"
#include "TCPPortProvider.h"
#include "concurrentqueue.h"
#include "tgfx/layers/Layer.h"

namespace tgfx::inspect {

class LayerTree final {
 public:
  static LayerTree& Get() {
    static LayerTree instance;
    return instance;
  }

  void setSelectLayer(std::shared_ptr<Layer> layer);

  void setDisplayList(DisplayList* list);

  void serializingLayerTree();

  void serializingLayerAttribute(const std::shared_ptr<Layer>& layer);

  void feedBackDataProcess(const std::vector<uint8_t>& data);

  void renderImageAndSend(Context* context);

  LayerTree(const LayerTree&) = delete;

  LayerTree(LayerTree&&) = delete;

  LayerTree& operator=(const LayerTree&) = delete;

  LayerTree& operator=(LayerTree&&) = delete;

  class SocketAgent {
   public:
    static SocketAgent& Get() {
      static SocketAgent instance;
      return instance;
    }

    void setData(const std::vector<uint8_t>& data);

    void setCallBack(std::function<void(const std::vector<uint8_t>&)> callback);

    SocketAgent(const SocketAgent&) = delete;

    SocketAgent(SocketAgent&&) = delete;

    SocketAgent& operator=(const SocketAgent&) = delete;

    SocketAgent& operator=(SocketAgent&&) = delete;

    ~SocketAgent();

   protected:
    SocketAgent();

    void sendWork();

    void recvWork();

    void spawnWorkTread();

   private:
#ifndef __EMSCRIPTEN__
    std::shared_ptr<ListenSocket> listenSocket = nullptr;
    std::shared_ptr<Socket> socket = nullptr;
    std::queue<std::vector<uint8_t>> messages = {};
    std::array<std::shared_ptr<UDPBroadcast>, BroadcastCount> broadcasts = {};
    bool isUDPOpened = true;
#endif
    int64_t epoch = 0;
    moodycamel::ConcurrentQueue<std::vector<uint8_t>> queue;
    std::shared_ptr<std::thread> sendThread = nullptr;
    std::shared_ptr<std::thread> recvThread = nullptr;
    std::function<void(const std::vector<uint8_t>&)> callback = nullptr;
    std::atomic<bool> stopFlag = false;
  };

 protected:
  void setCallBack();

  void addHighLightOverlay(Color color, std::shared_ptr<Layer> layer);

  void sendPickedLayerAddress(const std::shared_ptr<Layer>& layer);

  void sendFlushAttributeAck(uint64_t address);

  LayerTree();

 private:
  std::unordered_map<uint64_t, std::shared_ptr<Layer>> layerMap = {};
  std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>()>>>
      layerComplexObjMap = {};
  std::unordered_map<uint64_t,
                     std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>(Context*)>>>
      layerRenderableObjMap = {};
  uint64_t hoveredAddress = 0;
  uint64_t selectedAddress = 0;
  uint64_t expandID = 0;
  std::shared_ptr<Layer> hoverdLayer = nullptr;
  int highLightLayerIndex = 0;
  bool hoverdSwitch = false;
  DisplayList* displayList = nullptr;
};

}  // namespace tgfx::inspect
