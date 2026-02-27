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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#endif
#define poll WSAPoll
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ProcessUtils.h"
#include "Socket.h"
#include "TCPPortProvider.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace tgfx::inspect {
#ifdef _WIN32
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif

#ifdef _WIN32
struct __wsinit {
  __wsinit() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      fprintf(stderr, "Cannot init winsock.\n");
      exit(1);
    }
  }
};

void initWinSock() {
  static __wsinit init;
}
#endif

static constexpr size_t BufSize = 128 * 1024;

Socket::Socket() : buf((char*)malloc(BufSize)), sock(-1) {
#ifdef _WIN32
  initWinSock();
#endif
}

Socket::Socket(int sock) : buf((char*)malloc(BufSize)), sock(sock) {
}

Socket::~Socket() {
  free(buf);
  if (sock.load(std::memory_order_relaxed) != -1) {
    socketClose();
  }
  if (ptr) {
    freeaddrinfo(res);
#ifdef _WIN32
    closesocket(connSock);
#else
    close(connSock);
#endif
  }
}

bool Socket::connectAddress(const char* addr, uint16_t port) {
  assert(!isValid());

  if (ptr) {
    const auto c = connect(connSock, ptr->ai_addr, ptr->ai_addrlen);
    if (c == -1) {
#if defined _WIN32
      const auto err = WSAGetLastError();
      if (err == WSAEALREADY || err == WSAEINPROGRESS) {
        return false;
      }
      if (err != WSAEISCONN) {
        freeaddrinfo(res);
        closesocket(connSock);
        ptr = nullptr;
        return false;
      }
#else
      const auto err = errno;
      if (err == EALREADY || err == EINPROGRESS) {
        return false;
      }
      if (err != EISCONN) {
        freeaddrinfo(res);
        close(connSock);
        ptr = nullptr;
        return false;
      }
#endif
    }

#if defined _WIN32
    u_long nonblocking = 0;
    ioctlsocket(connSock, FIONBIO, &nonblocking);
#else
    int flags = fcntl(connSock, F_GETFL, 0);
    fcntl(connSock, F_SETFL, flags & ~O_NONBLOCK);
#endif
    this->sock.store(connSock, std::memory_order_relaxed);
    freeaddrinfo(res);
    ptr = nullptr;
    return true;
  }

  struct addrinfo hints = {};
  struct addrinfo* res = nullptr;
  struct addrinfo* ptr = nullptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char portbuf[32];
  snprintf(portbuf, 32, "%" PRIu16, port);

  if (getaddrinfo(addr, portbuf, &hints, &res) != 0) {
    return false;
  }
  int sock = 0;
  for (ptr = res; ptr; ptr = ptr->ai_next) {
    if ((sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
      continue;
    }
#if defined __APPLE__
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
#if defined _WIN32
    u_long nonblocking = 1;
    ioctlsocket(sock, FIONBIO, &nonblocking);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
    if (connect(sock, ptr->ai_addr, ptr->ai_addrlen) == 0) {
      break;
    } else {
#if defined _WIN32
      const auto err = WSAGetLastError();
      if (err != WSAEWOULDBLOCK) {
        closesocket(sock);
        continue;
      }
#else
      if (errno != EINPROGRESS) {
        close(sock);
        continue;
      }
#endif
    }
    this->res = res;
    this->ptr = ptr;
    connSock = sock;
    return false;
  }
  freeaddrinfo(res);
  if (!ptr) {
    return false;
  }

#if defined _WIN32
  u_long nonblocking = 0;
  ioctlsocket(sock, FIONBIO, &nonblocking);
#else
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
#endif

  this->sock.store(sock, std::memory_order_relaxed);
  return true;
}

bool Socket::connectBlocking(const char* addr, uint16_t port) {
  assert(!isValid());
  assert(!ptr);

  struct addrinfo hints = {};
  struct addrinfo* res = nullptr;
  struct addrinfo* ptr = nullptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char portbuf[32];
  snprintf(portbuf, 32, "%" PRIu16, port);

  if (getaddrinfo(addr, portbuf, &hints, &res) != 0) {
    return false;
  }
  int sock = 0;
  for (ptr = res; ptr; ptr = ptr->ai_next) {
    if ((sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
      continue;
    }
#if defined __APPLE__
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
    if (connect(sock, ptr->ai_addr, ptr->ai_addrlen) == -1) {
#ifdef _WIN32
      closesocket(sock);
#else
      close(sock);
#endif
      continue;
    }
    break;
  }
  freeaddrinfo(res);
  if (!ptr) {
    return false;
  }

  this->sock.store(sock, std::memory_order_relaxed);
  return true;
}

void Socket::socketClose() {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  assert(sock != -1);
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
  this->sock.store(-1, std::memory_order_relaxed);
}

int Socket::sendData(const void* buffer, size_t len) {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  auto buf = static_cast<const char*>(buffer);
  assert(sock != -1);
  auto start = buf;
  while (len > 0) {
    auto result = send(sock, buf, len, MSG_NOSIGNAL);
    if (result == -1) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      return -1;
    }
    len -= static_cast<size_t>(result);
    buf += result;
  }
  return int(buf - start);
}

int Socket::getSendBufferSize() {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  int bufSize;
#if defined _WIN32
  int sz = sizeof(bufSize);
  getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, &sz);
#else
  socklen_t sz = sizeof(bufSize);
  getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufSize, &sz);
#endif
  return bufSize;
}

int Socket::recvBuffered(void* buffer, size_t len, int timeout) {
  if (static_cast<int>(len) <= bufLeft) {
    memcpy(buffer, bufPtr, len);
    bufPtr += len;
    bufLeft -= len;
    return static_cast<int>(len);
  }

  if (bufLeft > 0) {
    memcpy(buffer, bufPtr, static_cast<size_t>(bufLeft));
    const auto result = bufLeft;
    bufLeft = 0;
    return result;
  }

  if (len >= BufSize) {
    return receiveData(buffer, len, timeout);
  }

  bufLeft = receiveData(buf, BufSize, timeout);
  if (bufLeft <= 0) {
    return bufLeft;
  }
  const auto sz = static_cast<int>(len) < bufLeft ? len : static_cast<size_t>(bufLeft);
  memcpy(buffer, buf, sz);
  bufPtr = buf + sz;
  bufLeft -= static_cast<int>(sz);
  return static_cast<int>(sz);
}

int Socket::receiveData(void* buffer, size_t len, int timeout) {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  auto buf = static_cast<char*>(buffer);

  struct pollfd fd;
  fd.fd = (socket_t)sock;
  fd.events = POLLIN;

  if (poll(&fd, 1, timeout) > 0) {
    return static_cast<int>(recv(sock, buf, len, 0));
  } else {
    return -1;
  }
}

int Socket::readUpTo(void* buffer, size_t len) {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  auto buf = static_cast<char*>(buffer);

  int rd = 0;
  while (len > 0) {
    const auto res = recv(sock, buf, len, 0);
    if (res == 0) {
      break;
    }
    if (res == -1) {
      return -1;
    }
    len -= static_cast<size_t>(res);
    rd += res;
    buf += res;
  }
  return rd;
}

bool Socket::readData(void* buf, size_t len, int timeout) {
  auto cbuf = static_cast<char*>(buf);
  while (len > 0) {
    if (!readImpl(cbuf, len, timeout)) {
      return false;
    }
  }
  return true;
}

bool Socket::readMaxLength(void* buf, size_t& maxLength, int timeout) {
  auto cbuf = static_cast<char*>(buf);
  if (!readImpl(cbuf, maxLength, timeout)) {
    return false;
  }
  return true;
}

bool Socket::readImpl(char*& buf, size_t& len, int timeout) {
  const auto sz = recvBuffered(buf, len, timeout);
  switch (sz) {
    case 0:
      return false;
    case -1:
#ifdef _WIN32
    {
      auto err = WSAGetLastError();
      if (err == WSAECONNABORTED || err == WSAECONNRESET) {
        return false;
      }
    }
#endif
    break;
    default:
      len -= static_cast<size_t>(sz);
      buf += sz;
      break;
  }
  return true;
}

bool Socket::readRaw(void* buffer, size_t len, int timeout) {
  auto buf = static_cast<char*>(buffer);
  while (len > 0) {
    const auto sz = receiveData(buf, len, timeout);
    if (sz <= 0) {
      return false;
    }
    len -= static_cast<size_t>(sz);
    buf += sz;
  }
  return true;
}

bool Socket::hasData() {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  if (bufLeft > 0) {
    return true;
  }

  struct pollfd fd = {};
  fd.fd = (socket_t)sock;
  fd.events = POLLIN;

  return poll(&fd, 1, 0) > 0;
}

bool Socket::isValid() const {
  return this->sock.load(std::memory_order_relaxed) >= 0;
}

ListenSocket::ListenSocket() : sock(-1), listenPort(0) {
#ifdef _WIN32
  initWinSock();
#endif
}

ListenSocket::~ListenSocket() {
  if (this->sock != -1) {
    closeSock();
  }
}

static int AddrinfoAndSocketForFamily(uint16_t port, int ai_family, struct addrinfo** res) {
  struct addrinfo hints = {};
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = ai_family;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  char portbuf[32];
  snprintf(portbuf, 32, "%" PRIu16, port);
  if (getaddrinfo(nullptr, portbuf, &hints, res) != 0) {
    return -1;
  }
  int sock = socket((*res)->ai_family, (*res)->ai_socktype, (*res)->ai_protocol);
  if (sock == -1) {
    freeaddrinfo(*res);
  }
  return sock;
}

bool ListenSocket::listenSock(uint16_t port, int backlog) {
  assert(this->sock == -1);

  struct addrinfo* res = nullptr;
  this->sock = AddrinfoAndSocketForFamily(port, AF_INET6, &res);
  if (this->sock == -1) {
    // IPV6 protocol may not be available/is disabled. Try to create a socket
    // with the IPV4 protocol
    this->sock = AddrinfoAndSocketForFamily(port, AF_INET, &res);
    if (this->sock == -1) {
      return false;
    }
  }
  listenPort = port;
#if defined _WIN32
  unsigned long val = 0;
  setsockopt(this->sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&val, sizeof(val));
#elif defined BSD
  int val = 0;
  setsockopt(this->sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&val, sizeof(val));
  val = 1;
  setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
#else
  int val = 1;
  setsockopt(this->sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
#endif
  if (bind(this->sock, res->ai_addr, res->ai_addrlen) == -1) {
    freeaddrinfo(res);
    closeSock();
    return false;
  }
  if (listen(this->sock, backlog) == -1) {
    freeaddrinfo(res);
    closeSock();
    return false;
  }
  freeaddrinfo(res);
  return true;
}

std::shared_ptr<Socket> ListenSocket::acceptSock() {
  struct sockaddr_storage remote = {};
  socklen_t sz = sizeof(remote);

  struct pollfd fd = {};
  fd.fd = (socket_t)this->sock;
  fd.events = POLLIN;

  if (poll(&fd, 1, 10) > 0) {
    int sock = accept(this->sock, (sockaddr*)&remote, &sz);
    if (sock == -1) {
      return nullptr;
    }

#if defined __APPLE__
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
    return std::make_shared<Socket>(sock);
  } else {
    return nullptr;
  }
}

void ListenSocket::closeSock() {
  assert(sock != -1);
#ifdef _WIN32
  closesocket(this->sock);
#else
  close(sock);
#endif
  sock = -1;
}

UDPBroadcast::UDPBroadcast() : sock(-1) {
#ifdef _WIN32
  initWinSock();
#endif
}

UDPBroadcast::~UDPBroadcast() {
  if (this->sock != -1) {
    closeSock();
  }
}

bool UDPBroadcast::openConnect(const char* addr, uint16_t port) {
  assert(this->sock == -1);

  struct addrinfo hints = {};
  struct addrinfo* res = nullptr;
  struct addrinfo* ptr = nullptr;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  char portbuf[32];
  snprintf(portbuf, 32, "%" PRIu16, port);

  if (getaddrinfo(addr, portbuf, &hints, &res) != 0) {
    return false;
  }
  int sock = 0;
  for (ptr = res; ptr; ptr = ptr->ai_next) {
    if ((sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
      continue;
    }
#if defined __APPLE__
    int val = 1;
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
#if defined _WIN32
    unsigned long broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast)) ==
        -1)
#else
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) == -1)
#endif
    {
#ifdef _WIN32
      closesocket(sock);
#else
      close(sock);
#endif
      continue;
    }
    break;
  }
  freeaddrinfo(res);
  if (!ptr) {
    return false;
  }

  this->sock = sock;
  inet_pton(AF_INET, addr, &this->addr);
  return true;
}

void UDPBroadcast::closeSock() {
  assert(sock != -1);
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
  sock = -1;
}

int UDPBroadcast::sendData(uint16_t port, const void* data, size_t len) {
  char strAddr[17] = {};
  inet_ntop(AF_INET, &this->addr, strAddr, 17);
  assert(this->sock != -1);
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = this->addr;
  return (int)sendto(this->sock, static_cast<const char*>(data), len, MSG_NOSIGNAL,
                     (sockaddr*)&addr, sizeof(addr));
}

IpAddress::IpAddress() : number(0) {
  *text = '\0';
}

IpAddress::~IpAddress() = default;

void IpAddress::setAddr(const struct sockaddr& addr) {
#if defined _WIN32 && (!defined NTDDI_WIN10 || NTDDI_VERSION < NTDDI_WIN10)
  struct sockaddr_in tmp;
  memcpy(&tmp, &addr, sizeof(tmp));
  auto addrIn = &tmp;
#else
  auto addrIn = (const struct sockaddr_in*)&addr;
#endif
  inet_ntop(AF_INET, &addrIn->sin_addr, text, 17);
  number = addrIn->sin_addr.s_addr;
}

UDPListen::UDPListen() : sock(-1) {
#ifdef _WIN32
  initWinSock();
#endif
}

UDPListen::~UDPListen() {
  if (this->sock != -1) {
    closeSock();
  }
}

bool UDPListen::listenSock(uint16_t port) {
  assert(this->sock == -1);

  int sock;
  if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    return false;
  }

#if defined __APPLE__
  int val = 1;
  setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
#if defined _WIN32
  unsigned long reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
#else
  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
#if defined _WIN32
  unsigned long broadcast = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast)) == -1)
#else
  int broadcast = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) == -1)
#endif
  {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return false;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == -1) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return false;
  }

  this->sock = sock;
  return true;
}

void UDPListen::closeSock() {
  assert(sock != -1);
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
  sock = -1;
}

const char* UDPListen::readData(size_t& len, IpAddress& addr, int timeout) {
  static char buf[2048];

  struct pollfd fd = {};
  fd.fd = (socket_t)this->sock;
  fd.events = POLLIN;
  if (poll(&fd, 1, timeout) <= 0) {
    return nullptr;
  }

  sockaddr sa = {};
  socklen_t salen = sizeof(struct sockaddr);
  len = (size_t)recvfrom(this->sock, buf, 2048, 0, &sa, &salen);
  addr.setAddr(sa);

  return buf;
}

}  // namespace tgfx::inspect
