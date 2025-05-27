#include "LayerProfiler.h"
#include <chrono>
#include <thread>
#include "Alloc.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/websocket.h>
#endif

namespace inspector {

static LayerProfiler s_layer_profiler;

void LayerProfiler::SendLayerData(const std::vector<uint8_t>& data) {
  s_layer_profiler.setData(data);
}
void LayerProfiler::SetLayerCallBack(std::function<void(const std::vector<uint8_t>&)> callback) {
  s_layer_profiler.setCallBack(callback);
}

LayerProfiler::LayerProfiler() {
#ifdef __EMSCRIPTEN__
  m_WebSocket = nullptr;
#endif
  spawnWorkTread();
}

LayerProfiler::~LayerProfiler() {
  m_StopFlag.store(true, std::memory_order_release);
  m_SendThread->join();
  m_RecvThread->join();
  if (m_Socket) {
    inspectorFree(m_Socket);
  }
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
  uint16_t port = 8084;
  printf("Start listen port: %d!\n", port);
  bool isListen = m_ListenSocket.Listen(port, 4);
  if (!isListen) {
    printf("Listen port: %d return false!\n", port);
  }
  while (!m_StopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    //m_Socket = std::shared_ptr<Socket>(m_ListenSocket.Accept());
    m_Socket = m_ListenSocket.Accept();
    if (m_Socket) {
      printf("tcp already connect!\n");
      break;
    }
  }

  while (!m_StopFlag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!m_Queue.empty()) {
      auto data = *m_Queue.front();
      m_Queue.pop();
      m_Socket->Send(data.data(), data.size());
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
      m_Socket->ReadUpTo(&size, sizeof(int));
      std::vector<uint8_t> data(static_cast<size_t>(size));
      m_Socket->ReadUpTo(data.data(), (size_t)size);
      messages.push(std::move(data));
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
