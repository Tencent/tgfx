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

#include <atomic>
#include <cstdint>
#include <memory>

namespace tgfx::inspect {
class Socket {
 public:
  explicit Socket(int sock);

  Socket();

  ~Socket();

  bool connectAddress(const char* addr, uint16_t port);

  bool connectBlocking(const char* addr, uint16_t port);

  void socketClose();

  int sendData(const void* buf, size_t len);

  int getSendBufferSize();

  int readUpTo(void* buf, size_t len);

  bool readData(void* buf, size_t len, int timeout);

  bool readMaxLength(void* buf, size_t& maxLength, int timeout);

  bool readRaw(void* buf, size_t len, int timeout);

  bool hasData();

  bool isValid() const;

  template <typename ShouldExit>
  bool readData(void* buf, size_t len, int timeout, ShouldExit exitCb) {
    auto cbuf = static_cast<char*>(buf);
    while (len > 0) {
      if (exitCb()) {
        return false;
      }
      if (!readImpl(cbuf, len, timeout)) {
        return false;
      }
    }
    return true;
  }

  Socket(const Socket&) = delete;

  Socket(Socket&&) = delete;

  Socket& operator=(const Socket&) = delete;

  Socket& operator=(Socket&&) = delete;

 protected:
  int recvBuffered(void* buf, size_t len, int timeout);

  int receiveData(void* buf, size_t len, int timeout);

  bool readImpl(char*& buf, size_t& len, int timeout);

 private:
  char* buf = nullptr;
  char* bufPtr = nullptr;
  std::atomic<int> sock = 0;
  int bufLeft = 0;
  struct addrinfo* res = nullptr;
  struct addrinfo* ptr = nullptr;
  int connSock = 0;
};

class ListenSocket {
 public:
  ListenSocket();

  ~ListenSocket();

  bool listenSock(uint16_t port, int backlog);

  std::shared_ptr<Socket> acceptSock();

  void closeSock();

  ListenSocket(const ListenSocket&) = delete;

  ListenSocket(ListenSocket&&) = delete;

  ListenSocket& operator=(const ListenSocket&) = delete;

  ListenSocket& operator=(ListenSocket&&) = delete;

 private:
  int sock = 0;
  uint16_t listenPort = 0;
};

class UDPBroadcast {
 public:
  UDPBroadcast();

  ~UDPBroadcast();

  bool openConnect(const char* addr, uint16_t port);

  void closeSock();

  int sendData(uint16_t port, const void* data, size_t len);

  UDPBroadcast(const UDPBroadcast&) = delete;

  UDPBroadcast(UDPBroadcast&&) = delete;

  UDPBroadcast& operator=(const UDPBroadcast&) = delete;

  UDPBroadcast& operator=(UDPBroadcast&&) = delete;

 private:
  int sock = 0;
  uint32_t addr = 0;
};

class IpAddress {
 public:
  IpAddress();

  ~IpAddress();

  uint32_t getNumber() const {
    return number;
  }

  const char* getText() const {
    return text;
  }

  void setAddr(const struct sockaddr& addr);

  IpAddress(const IpAddress&) = delete;

  IpAddress(IpAddress&&) = delete;

  IpAddress& operator=(const IpAddress&) = delete;

  IpAddress& operator=(IpAddress&&) = delete;

 private:
  uint32_t number = 0;
  char text[17] = {};
};

class UDPListen {
 public:
  UDPListen();

  ~UDPListen();

  bool listenSock(uint16_t port);

  void closeSock();

  const char* readData(size_t& len, IpAddress& addr, int timeout);

  UDPListen(const UDPListen&) = delete;

  UDPListen(UDPListen&&) = delete;

  UDPListen& operator=(const UDPListen&) = delete;

  UDPListen& operator=(UDPListen&&) = delete;

 private:
  int sock = 0;
};

}  // namespace tgfx::inspect
