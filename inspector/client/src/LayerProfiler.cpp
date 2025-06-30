#include "LayerProfiler.h"
#include <chrono>
#include <thread>
#include "Alloc.h"
#include "Protocol.h"
#include "Utils.h"

namespace inspector {

static uint16_t port = 8084;
static LayerProfiler s_layer_profiler;

static const char* addr = "255.255.255.255";
static uint16_t broadcastPort = 8086;

void LayerProfiler::SendLayerData(const std::vector<uint8_t>& data) {
  s_layer_profiler.setData(data);
}
void LayerProfiler::SetLayerCallBack(std::function<void(const std::vector<uint8_t>&)> callback) {
  s_layer_profiler.setCallBack(callback);
}

LayerProfiler::LayerProfiler() {
#ifdef __EMSCRIPTEN__
#else
  listenSocket = new ListenSocket();
  for (uint16_t i = 0; i < broadcastNum; i++) {
    broadcast[i] = new UdpBroadcast();
    isUDPOpened = isUDPOpened && broadcast[i]->Open(addr, broadcastPort + i);
  }

#endif
  epoch = std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
  spawnWorkTread();
}

LayerProfiler::~LayerProfiler() {
  stopFlag.store(true, std::memory_order_release);
  sendThread->join();
  recvThread->join();
#ifdef __EMSCRIPTEN__
#else
  if (socket) {
    inspectorFree(socket);
  }
  if (listenSocket) {
    delete listenSocket;
  }
  for (uint16_t i = 0; i < broadcastNum; i++) {
    if (broadcast[i]) {
      delete broadcast[i];
    }
  }
#endif
}

void LayerProfiler::sendWork() {
#ifdef __EMSCRIPTEN__
#else
  if (!isUDPOpened) {
    return;
  }
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);
  bool isListening = false;
  for (uint16_t i = 0; i < 20; ++i) {
    if (listenSocket->Listen(port - i, 4)) {
      port -= i;
      isListening = true;
      break;
    }
  }
  if (!isListening) {
    return;
  }

  size_t broadcastLen = 0;
  auto broadcastMsg = GetBroadcastMessage(procname, pnsz, broadcastLen, port, LayerTree);
  long long lastBroadcast = 0;
  while (!stopFlag.load(std::memory_order_acquire)) {
    while (!stopFlag.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      const auto t = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      if (t - lastBroadcast > 3000000000) {
        lastBroadcast = t;
        for (uint16_t i = 0; i < broadcastNum; i++) {
          if (broadcast[i]) {
            const auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
            broadcastMsg.activeTime = int32_t(ts - epoch);
            broadcast[i]->Send(broadcastPort + i, &broadcastMsg, broadcastLen);
          }
        }
      }
      socket = listenSocket->Accept();
      if (socket) {
        printf("tcp already connect!\n");
        break;
      }
    }

    while (!stopFlag.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (!socket) {
        break;
      }
      if (!queue.empty()) {
        auto data = *queue.front();
        queue.pop();
        int size = (int)data.size();
        if (socket) {
          socket->Send(&size, sizeof(int));
          socket->Send(data.data(), data.size());
        }
      }
    }
  }
#endif
}

void LayerProfiler::recvWork() {
#ifdef __EMSCRIPTEN__
#else
  while (!stopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (socket && socket->HasData()) {
      int size;
      int flag = socket->ReadUpTo(&size, sizeof(int));
      std::vector<uint8_t> data(static_cast<size_t>(size));
      int flag1 = socket->ReadUpTo(data.data(), (size_t)size);
      messages.push(std::move(data));
      if (!flag || !flag1) {
        inspectorFree(socket);
        socket = nullptr;
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
  queue.push(data);
}
void LayerProfiler::setCallBack(std::function<void(const std::vector<uint8_t>&)> callback) {
  if (!this->callback) {
    this->callback = callback;
  }
}
}  // namespace inspector
