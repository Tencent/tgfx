#include "LayerProfiler.h"
#include <chrono>
#include <thread>
#include "Alloc.h"
#include "Protocol.h"
#include "Utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/websocket.h>
#endif

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
  m_WebSocket = nullptr;
#else
  m_ListenSocket = new ListenSocket();
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
  m_StopFlag.store(true, std::memory_order_release);
  m_SendThread->join();
  m_RecvThread->join();
#ifdef __EMSCRIPTEN__
#else
  if (m_Socket) {
    inspectorFree(m_Socket);
  }
  if (m_ListenSocket) {
    delete m_ListenSocket;
  }
  for (uint16_t i = 0; i < broadcastNum; i++) {
    if (broadcast[i]) {
      delete broadcast[i];
    }
  }
#endif
}

void LayerProfiler::SendWork() {
#ifdef __EMSCRIPTEN__
  // create websocket
  while (!m_StopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    m_WebSocket = std::make_shared<WebSocketClient>("ws://localhost:8085");
    if (m_WebSocket->isConnect) {
      printf("web socket create success!\n");
      break;
    }
    printf("web socket create failure!\n");
    m_WebSocket.reset();
  }

  while (!m_StopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!m_WebSocket->isConnect) break;
    if (!m_Queue.empty()) {
      auto data = *m_Queue.front();
      m_Queue.pop();
      m_WebSocket->sendMessage((char*)data.data(), data.size());
    }
  }
#else
  if (!isUDPOpened) {
    return;
  }
  const auto procname = GetProcessName();
  const auto pnsz = std::min<size_t>(strlen(procname), WelcomeMessageProgramNameSize - 1);
  bool isListening = false;
  for (uint16_t i = 0; i < 20; ++i) {
    if (m_ListenSocket->Listen(port - i, 4)) {
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
  while (!m_StopFlag.load(std::memory_order_acquire)) {
    while (!m_StopFlag.load(std::memory_order_acquire)) {
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
      m_Socket = m_ListenSocket->Accept();
      if (m_Socket) {
        printf("tcp already connect!\n");
        break;
      }
    }

    while (!m_StopFlag.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (!m_Socket) {
        break;
      }
      if (!m_Queue.empty()) {
        auto data = *m_Queue.front();
        m_Queue.pop();
        int size = (int)data.size();
        if (m_Socket) {
          m_Socket->Send(&size, sizeof(int));
          m_Socket->Send(data.data(), data.size());
        }
      }
    }
  }
#endif
}

void LayerProfiler::recvWork() {
#ifdef __EMSCRIPTEN__
  while (!m_StopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    WebSocketClient::Message message;
    if (m_WebSocket->recvMssageImmdiately(message)) {
      printf("recive message type: %d \n", message.type);
      if ((message.type == WebSocketClient::Binary) && m_Callback) {
        std::vector<uint8_t> data(message.data.data(), message.data.data() + message.data.size());
        m_Callback(data);
      }
    }
  }
#else
  while (!m_StopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (m_Socket && m_Socket->HasData()) {
      int size;
      int flag = m_Socket->ReadUpTo(&size, sizeof(int));
      std::vector<uint8_t> data(static_cast<size_t>(size));
      int flag1 = m_Socket->ReadUpTo(data.data(), (size_t)size);
      messages.push(std::move(data));
      if (!flag || !flag1) {
        inspectorFree(m_Socket);
        m_Socket = nullptr;
      }
    }
    if (!messages.empty()) {
      if (m_Callback) {
        m_Callback(messages.front());
        messages.pop();
      }
    }
  }
#endif
}

void LayerProfiler::spawnWorkTread() {
  m_StopFlag.store(false, std::memory_order_release);
  m_SendThread = std::make_shared<std::thread>(&LayerProfiler::SendWork, this);
  m_RecvThread = std::make_shared<std::thread>(&LayerProfiler::recvWork, this);
}

void LayerProfiler::setData(const std::vector<uint8_t>& data) {
  m_Queue.push(data);
}
void LayerProfiler::setCallBack(std::function<void(const std::vector<uint8_t>&)> callback) {
  if (!m_Callback) {
    m_Callback = callback;
  }
}
}  // namespace inspector
