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
#include "LayerTree.h"
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include "ProcessUtils.h"
#include "Protocol.h"
#include "concurrentqueue.h"
#include "inspect/InspectorMark.h"
#include "serialization/LayerSerialization.h"
#include "tgfx/core/Clock.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"

namespace tgfx::inspect {
static moodycamel::ConcurrentQueue<uint64_t> imageIDQueue;
void LayerTree::setSelectLayer(std::shared_ptr<Layer> layer) {
  if (layer->name() != HighLightLayerName) {
    if (reinterpret_cast<uint64_t>(layer.get()) != selectedAddress) {
      sendPickedLayerAddress(layer);
    }
    if (hoverdSwitch) {
      addHighLightOverlay(Color::FromRGBA(111, 166, 219), std::move(layer));
    }
  }
}

void LayerTree::setCallBack() {
  std::function<void(const std::vector<uint8_t>&)> func = [this](const std::vector<uint8_t>& data) {
    feedBackDataProcess(data);
  };
  LAYER_CALLBACK(func);
}

void LayerTree::renderImageAndSend(Context* context) {
  if (imageIDQueue.size_approx() != 0) {
    uint64_t id;
    if (imageIDQueue.try_dequeue(id)) {
      if (layerRenderableObjMap.find(selectedAddress) != layerRenderableObjMap.end() &&
          layerRenderableObjMap[selectedAddress].find(id) !=
              layerRenderableObjMap[selectedAddress].end()) {
        std::shared_ptr<Data> data = layerRenderableObjMap[selectedAddress][id](context);
        if (!data->empty()) {
          std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
          SEND_LAYER_DATA(blob);
        }
      }
    }
  }
}

LayerTree::LayerTree() {
  setCallBack();
}

void LayerTree::setDisplayList(DisplayList* list) {
  displayList = list;
}

void LayerTree::serializingLayerTree() {
  layerMap.clear();

  std::shared_ptr<Data> data =
      LayerSerialization::SerializeTreeNode(displayList->root()->shared_from_this(), layerMap);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());

  SEND_LAYER_DATA(blob);
}

void LayerTree::sendPickedLayerAddress(const std::shared_ptr<Layer>& layer) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.UInt("Type", static_cast<uint8_t>(LayerTreeMessage::PickedLayerAddress));
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", reinterpret_cast<uint64_t>(layer.get()));
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  SEND_LAYER_DATA(fbb.GetBuffer());
}

void LayerTree::sendFlushAttributeAck(uint64_t address) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.UInt("Type", static_cast<uint8_t>(LayerTreeMessage::FlushAttributeAck));
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", address);
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  SEND_LAYER_DATA(fbb.GetBuffer());
}

void LayerTree::serializingLayerAttribute(const std::shared_ptr<Layer>& layer) {
  if (!layer) {
    return;
  }
  auto& complexObjSerMap = layerComplexObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto& renderableObjSerMap = layerRenderableObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto data = LayerSerialization::SerializeLayer(
      layer.get(), &complexObjSerMap, &renderableObjSerMap, LayerTreeMessage::LayerAttribute);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
  SEND_LAYER_DATA(blob);
}

void LayerTree::feedBackDataProcess(const std::vector<uint8_t>& data) {
  if (data.size() == 0) {
    return;
  }
  auto map = flexbuffers::GetRoot(data.data(), data.size()).AsMap();
  auto type = static_cast<LayerTreeMessage>(map["Type"].AsUInt8());
  switch (type) {
    case LayerTreeMessage::EnableLayerInspector: {
      hoverdSwitch = map["Value"].AsUInt64();
      if (!hoverdSwitch && hoverdLayer) {
        hoverdLayer->removeChildren(highLightLayerIndex);
        hoverdLayer = nullptr;
      }
      break;
    }
    case LayerTreeMessage::HoverLayerAddress: {
      if (hoverdSwitch) {
        hoveredAddress = map["Value"].AsUInt64();
        addHighLightOverlay(Color::FromRGBA(111, 166, 219), layerMap[hoveredAddress]);
      }
      break;
    }
    case LayerTreeMessage::SelectedLayerAddress: {
      selectedAddress = map["Value"].AsUInt64();
      break;
    }
    case LayerTreeMessage::SerializeAttribute: {
      serializingLayerAttribute(layerMap[selectedAddress]);
      break;
    }
    case LayerTreeMessage::SerializeSubAttribute: {
      expandID = map["Value"].AsUInt64();
      if (layerComplexObjMap.find(selectedAddress) != layerComplexObjMap.end() &&
          layerComplexObjMap[selectedAddress].find(expandID) !=
              layerComplexObjMap[selectedAddress].end()) {
        std::shared_ptr<Data> data = layerComplexObjMap[selectedAddress][expandID]();
        std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
        SEND_LAYER_DATA(blob);
      }
      break;
    }
    case LayerTreeMessage::FlushAttribute: {
      uint64_t address = map["Value"].AsUInt64();
      if (layerComplexObjMap.find(address) != layerComplexObjMap.end()) {
        layerComplexObjMap.erase(address);
      }
      if (layerRenderableObjMap.find(address) != layerRenderableObjMap.end()) {
        layerRenderableObjMap.erase(address);
      }
      sendFlushAttributeAck(address);
      break;
    }
    case LayerTreeMessage::FlushLayerTree: {
      serializingLayerTree();
      break;
    }
    case LayerTreeMessage::FlushImage: {
      uint64_t imageId = map["Value"].AsUInt64();
      imageIDQueue.enqueue(imageId);
      break;
    }
    default: {
      DEBUG_ASSERT(false);
    }
  }
}

void LayerTree::addHighLightOverlay(Color color, std::shared_ptr<Layer> layer) {
  if (!layer) {
    return;
  }

  if (layer == hoverdLayer) {
    return;
  }

  if (hoverdLayer) {
    hoverdLayer->removeChildren(highLightLayerIndex);
  }

  hoverdLayer = std::move(layer);
  auto highlightLayer = ShapeLayer::Make();
  highlightLayer->setName(HighLightLayerName);
  highlightLayer->setBlendMode(BlendMode::SrcOver);
  auto rectPath = Path();
  rectPath.addRect(hoverdLayer->getBounds());
  highlightLayer->setFillStyle(SolidColor::Make(color));
  highlightLayer->setPath(rectPath);
  highlightLayer->setAlpha(0.66f);
  hoverdLayer->addChild(highlightLayer);
  highLightLayerIndex = hoverdLayer->getChildIndex(highlightLayer);
}
#ifndef __EMSCRIPTEN__
static const char* addr = "255.255.255.255";
#endif
LayerTree::SocketAgent::SocketAgent() {
#ifndef __EMSCRIPTEN__
  listenSocket = std::make_shared<ListenSocket>();
  for (uint16_t i = 0; i < BroadcastCount; i++) {
    broadcasts[i] = std::make_shared<UDPBroadcast>();
    isUDPOpened = isUDPOpened && broadcasts[i]->openConnect(addr, BroadcastPort + i);
  }

#endif
  epoch = Clock::Now();
  spawnWorkTread();
}

LayerTree::SocketAgent::~SocketAgent() {
  stopFlag.store(true, std::memory_order_release);
  if (sendThread && sendThread->joinable()) {
    sendThread->join();
  }
  if (recvThread && recvThread->joinable()) {
    recvThread->join();
  }
}

void LayerTree::SocketAgent::sendWork() {
#ifndef __EMSCRIPTEN__
  if (!isUDPOpened) {
    return;
  }
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);
  auto port = TCPPortProvider::Get().getValidPort();
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
      const auto currentTime = Clock::Now();
      if (currentTime - lastBroadcast > BroadcastHeartbeatUSTime) {
        lastBroadcast = currentTime;
        for (uint16_t i = 0; i < BroadcastCount; i++) {
          if (broadcasts[i]) {
            const auto ts = Clock::Now();
            broadcastMsg.activeTime = static_cast<int32_t>(ts - epoch);
            broadcasts[i]->sendData(BroadcastPort + i, &broadcastMsg, broadcastLen);
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

void LayerTree::SocketAgent::recvWork() {
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

void LayerTree::SocketAgent::spawnWorkTread() {
  stopFlag.store(false, std::memory_order_release);
  sendThread = std::make_shared<std::thread>(&SocketAgent::sendWork, this);
  recvThread = std::make_shared<std::thread>(&SocketAgent::recvWork, this);
}

void LayerTree::SocketAgent::setData(const std::vector<uint8_t>& data) {
  queue.enqueue(data);
}
void LayerTree::SocketAgent::setCallBack(
    std::function<void(const std::vector<uint8_t>&)> callback) {
  this->callback = std::move(callback);
}
}  // namespace tgfx::inspect
