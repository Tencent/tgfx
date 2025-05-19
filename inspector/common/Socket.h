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

#include <atomic>
#include <cstdint>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <queue>
#include <emscripten.h>
#include <emscripten/websocket.h>
#include <emscripten/threading.h>
#endif

namespace inspector {

#ifdef __EMSCRIPTEN__
struct WebSocketClient {
  enum MessageType
  {
    Text,
    Binary,
    Close,
};

  struct Message
  {
    MessageType type;
    std::string data;
    bool readRaw(std::string& buf, int len);
  };

  EMSCRIPTEN_WEBSOCKET_T ws;
  bool isConnect = false;
  std::queue<Message> queue;
  std::string error;
  Message buffer;

  WebSocketClient(const char* url);
  bool hasMessage();
  bool sendMessage(char* text, int textLength);
  Message* recvMssage();
  bool recvMssageImmdiately(Message& message);
  static EM_BOOL onOpen(int eventType, const EmscriptenWebSocketOpenEvent *websocketEvent, void *userData);
  static EM_BOOL onClose(int eventType, const EmscriptenWebSocketCloseEvent *websocketEvent, void *userData);
  static EM_BOOL onError(int eventType, const EmscriptenWebSocketErrorEvent *websocketEvent, void *userData);
  static EM_BOOL onMessage(int eventType, const EmscriptenWebSocketMessageEvent *websocketEvent, void *userData);
};
#endif

class Socket {
 public:
  Socket();
  Socket(int sock);
  ~Socket();

  bool Connect(const char* addr, uint16_t port);
  bool ConnectBlocking(const char* addr, uint16_t port);
  void Close();

  int Send(const void* buf, size_t len);
  int GetSendBufSize();

  int ReadUpTo(void* buf, size_t len);
  bool Read(void* buf, size_t len, int timeout);
  bool ReadMax(void* buf, size_t& maxLen, int timeout);

  template <typename ShouldExit>
  bool Read(void* buf, size_t len, int timeout, ShouldExit exitCb) {
    auto cbuf = (char*)buf;
    while (len > 0) {
      if (exitCb()) return false;
      if (!ReadImpl(cbuf, len, timeout)) return false;
    }
    return true;
  }

  bool ReadRaw(void* buf, size_t len, int timeout);
  bool HasData();
  bool IsValid() const;

  Socket(const Socket&) = delete;
  Socket(Socket&&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket& operator=(Socket&&) = delete;

 private:
  ssize_t RecvBuffered(void* buf, size_t len, int timeout);
  ssize_t Recv(void* buf, size_t len, int timeout);

  bool ReadImpl(char*& buf, size_t& len, int timeout);

  char* buf;
  char* bufPtr;
  std::atomic<int> sock;
  ssize_t bufLeft;

  struct addrinfo* res;
  struct addrinfo* ptr;
  int connSock;
};

class ListenSocket {
 public:
  ListenSocket();
  ~ListenSocket();

  bool Listen(uint16_t port, int backlog);
  Socket* Accept();
  void Close();

  ListenSocket(const ListenSocket&) = delete;
  ListenSocket(ListenSocket&&) = delete;
  ListenSocket& operator=(const ListenSocket&) = delete;
  ListenSocket& operator=(ListenSocket&&) = delete;

 private:
  int sock;
};

class UdpBroadcast {
 public:
  UdpBroadcast();
  ~UdpBroadcast();

  bool Open(const char* addr, uint16_t port);
  void Close();

  ssize_t Send(uint16_t port, const void* data, size_t len);

  UdpBroadcast(const UdpBroadcast&) = delete;
  UdpBroadcast(UdpBroadcast&&) = delete;
  UdpBroadcast& operator=(const UdpBroadcast&) = delete;
  UdpBroadcast& operator=(UdpBroadcast&&) = delete;

 private:
  int sock;
  uint32_t addr;
};

class IpAddress {
 public:
  IpAddress();
  ~IpAddress();

  void Set(const struct sockaddr& addr);

  uint32_t GetNumber() const {
    return number;
  }
  const char* GetText() const {
    return text;
  }

  IpAddress(const IpAddress&) = delete;
  IpAddress(IpAddress&&) = delete;
  IpAddress& operator=(const IpAddress&) = delete;
  IpAddress& operator=(IpAddress&&) = delete;

 private:
  uint32_t number;
  char text[17];
};

class UdpListen {
 public:
  UdpListen();
  ~UdpListen();

  bool Listen(uint16_t port);
  void Close();

  const char* Read(size_t& len, IpAddress& addr, int timeout);

  UdpListen(const UdpListen&) = delete;
  UdpListen(UdpListen&&) = delete;
  UdpListen& operator=(const UdpListen&) = delete;
  UdpListen& operator=(UdpListen&&) = delete;

 private:
  int sock;
};

}  // namespace inspector