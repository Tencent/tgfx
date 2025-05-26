#pragma once
#include "Socket.h"
#include <queue>
#include <vector>
#include <functional>
#include <thread>
#include "LockFreeQueue.h"
namespace inspector
{
class LayerProfiler
{
public:
  static void SendLayerData(const std::vector<uint8_t>& data);
  static void SetLayerCallBack(std::function<void(const std::vector<uint8_t>&)> callback);
  LayerProfiler();
  ~LayerProfiler();
  void setData(const std::vector<uint8_t>& data);
  void setCallBack(std::function<void(const std::vector<uint8_t>&)> callback);
private:
  void SendWork();
  void recvWork();
  void spawnWorkTread();
private:
#ifdef __EMSCRIPTEN__
  std::shared_ptr<WebSocketClient> m_WebSocket;
#else
  ListenSocket m_ListenSocket;
  Socket* m_Socket = nullptr;
  std::queue<std::vector<uint8_t>> messages;
#endif
  LockFreeQueue<std::vector<uint8_t>> m_Queue;
  std::shared_ptr<std::thread> m_SendThread;
  std::shared_ptr<std::thread> m_RecvThread;
  std::function<void(const std::vector<uint8_t>&)> m_Callback;
  std::atomic<bool> m_StopFlag = false;
};
}


