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
#endif

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include "Alloc.h"
#include "Socket.h"
#include "Utils.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace inspector {
#ifdef _WIN32
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif

enum { BufSize = 128 * 1024 };

Socket::Socket()
    : buf((char*)inspectorMalloc(BufSize)), bufPtr(nullptr), sock(-1), bufLeft(0), res(nullptr),
      ptr(nullptr), connSock(0) {
}

Socket::Socket(int sock)
    : buf((char*)inspectorMalloc(BufSize)), bufPtr(nullptr), sock(sock), bufLeft(0), res(nullptr),
      ptr(nullptr), connSock(0) {
}

Socket::~Socket() {
  inspectorFree(buf);
  if (sock.load(std::memory_order_relaxed) != -1) {
    Close();
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

bool Socket::Connect(const char* addr, uint16_t port) {
  assert(!IsValid());

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

  struct addrinfo hints;
  struct addrinfo *res, *ptr;

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

bool Socket::ConnectBlocking(const char* addr, uint16_t port) {
  assert(!IsValid());
  assert(!ptr);

  struct addrinfo hints;
  struct addrinfo *res, *ptr;

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

void Socket::Close() {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  assert(sock != -1);
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
  this->sock.store(-1, std::memory_order_relaxed);
}

int Socket::Send(const void* _buf, size_t len) {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  auto buf = (const char*)_buf;
  assert(sock != -1);
  auto start = buf;
  while (len > 0) {
    auto ret = send(sock, buf, len, MSG_NOSIGNAL);
    if (ret == -1) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      printf("%s\n", strerror(errno));
      return -1;
    }
    len -= static_cast<size_t>(ret);
    buf += ret;
  }
  return int(buf - start);
}

int Socket::GetSendBufSize() {
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

int Socket::RecvBuffered(void* buf, size_t len, int timeout) {
  if (static_cast<int>(len) <= bufLeft) {
    memcpy(buf, bufPtr, len);
    bufPtr += len;
    bufLeft -= len;
    return static_cast<int>(len);
  }

  if (bufLeft > 0) {
    memcpy(buf, bufPtr, static_cast<size_t>(bufLeft));
    const auto ret = bufLeft;
    bufLeft = 0;
    return ret;
  }

  if (len >= BufSize) {
    return Recv(buf, len, timeout);
  }

  bufLeft = Recv(this->buf, BufSize, timeout);
  if (bufLeft <= 0) {
    return bufLeft;
  }
  const auto sz = static_cast<int>(len) < bufLeft ? len : static_cast<size_t>(bufLeft);
  memcpy(buf, this->buf, sz);
  bufPtr = this->buf + sz;
  bufLeft -= static_cast<int>(sz);
  return static_cast<int>(sz);
}

int Socket::Recv(void* _buf, size_t len, int timeout) {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  auto buf = (char*)_buf;

  struct pollfd fd;
  fd.fd = (socket_t)sock;
  fd.events = POLLIN;

  if (poll(&fd, 1, timeout) > 0) {
    return static_cast<int>(recv(sock, buf, len, 0));
  } else {
    return -1;
  }
}

int Socket::ReadUpTo(void* _buf, size_t len) {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  auto buf = (char*)_buf;

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

bool Socket::Read(void* buf, size_t len, int timeout) {
  auto cbuf = (char*)buf;
  while (len > 0) {
    if (!ReadImpl(cbuf, len, timeout)) {
      return false;
    }
  }
  return true;
}

bool Socket::ReadMax(void* buf, size_t& maxLen, int timeout) {
  auto cbuf = (char*)buf;
  if (!ReadImpl(cbuf, maxLen, timeout)) {
    return false;
  }
  return true;
}

bool Socket::ReadImpl(char*& buf, size_t& len, int timeout) {
  const auto sz = RecvBuffered(buf, len, timeout);
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

bool Socket::ReadRaw(void* _buf, size_t len, int timeout) {
  auto buf = (char*)_buf;
  while (len > 0) {
    const auto sz = Recv(buf, len, timeout);
    if (sz <= 0) {
      return false;
    }
    len -= static_cast<size_t>(sz);
    buf += sz;
  }
  return true;
}

bool Socket::HasData() {
  const auto sock = this->sock.load(std::memory_order_relaxed);
  if (bufLeft > 0) {
    return true;
  }

  struct pollfd fd;
  fd.fd = (socket_t)sock;
  fd.events = POLLIN;

  return poll(&fd, 1, 0) > 0;
}

bool Socket::IsValid() const {
  return this->sock.load(std::memory_order_relaxed) >= 0;
}

ListenSocket::ListenSocket() : sock(-1) {
#ifdef _WIN32
// InitWinSock();
#endif
}

ListenSocket::~ListenSocket() {
  if (this->sock != -1) {
    Close();
  }
}

static int addrinfo_and_socket_for_family(uint16_t port, int ai_family, struct addrinfo** res) {
  struct addrinfo hints;
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

bool ListenSocket::Listen(uint16_t port, int backlog) {
  assert(this->sock == -1);

  struct addrinfo* res = nullptr;
  this->sock = addrinfo_and_socket_for_family(port, AF_INET6, &res);
  if (this->sock == -1) {
    // IPV6 protocol may not be available/is disabled. Try to create a socket
    // with the IPV4 protocol
    this->sock = addrinfo_and_socket_for_family(port, AF_INET, &res);
    if (this->sock == -1) {
      return false;
    }
  }
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
    printf("bind error! %s\n", strerror(errno));
    freeaddrinfo(res);
    Close();
    return false;
  }
  if (listen(this->sock, backlog) == -1) {
    printf("listen error! %s\n", strerror(errno));
    freeaddrinfo(res);
    Close();
    return false;
  }
  freeaddrinfo(res);
  return true;
}

Socket* ListenSocket::Accept() {
  struct sockaddr_storage remote;
  socklen_t sz = sizeof(remote);

  struct pollfd fd;
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

    auto ptr = (Socket*)inspectorMalloc(sizeof(Socket));
    new (ptr) Socket(sock);
    return ptr;
  } else {
    return nullptr;
  }
}

void ListenSocket::Close() {
  assert(this->sock != -1);
#ifdef _WIN32
  closesocket(this->sock);
#else
  close(this->sock);
#endif
  this->sock = -1;
}

UdpBroadcast::UdpBroadcast() : sock(-1) {
#ifdef _WIN32
// InitWinSock();
#endif
}

UdpBroadcast::~UdpBroadcast() {
  if (this->sock != -1) {
    Close();
  }
}

bool UdpBroadcast::Open(const char* addr, uint16_t port) {
  assert(this->sock == -1);

  struct addrinfo hints;
  struct addrinfo *res, *ptr;

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

void UdpBroadcast::Close() {
  assert(this->sock != -1);
#ifdef _WIN32
  closesocket(this->sock);
#else
  close(this->sock);
#endif
  this->sock = -1;
}

int UdpBroadcast::Send(uint16_t port, const void* data, size_t len) {
  char strAddr[17];
  inet_ntop(AF_INET, &this->addr, strAddr, 17);
  assert(this->sock != -1);
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = this->addr;
  return (int)sendto(this->sock, (const char*)data, len, MSG_NOSIGNAL, (sockaddr*)&addr,
                     sizeof(addr));
}

IpAddress::IpAddress() : number(0) {
  *text = '\0';
}

IpAddress::~IpAddress() = default;

void IpAddress::Set(const struct sockaddr& addr) {
#if defined _WIN32 && (!defined NTDDI_WIN10 || NTDDI_VERSION < NTDDI_WIN10)
  struct sockaddr_in tmp;
  memcpy(&tmp, &addr, sizeof(tmp));
  auto ai = &tmp;
#else
  auto ai = (const struct sockaddr_in*)&addr;
#endif
  inet_ntop(AF_INET, &ai->sin_addr, text, 17);
  number = ai->sin_addr.s_addr;
}

UdpListen::UdpListen() : sock(-1) {
#ifdef _WIN32
// InitWinSock();
#endif
}

UdpListen::~UdpListen() {
  if (this->sock != -1) {
    Close();
  }
}

bool UdpListen::Listen(uint16_t port) {
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

  struct sockaddr_in addr;
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

void UdpListen::Close() {
  assert(this->sock != -1);
#ifdef _WIN32
  closesocket(this->sock);
#else
  close(this->sock);
#endif
  this->sock = -1;
}

const char* UdpListen::Read(size_t& len, IpAddress& addr, int timeout) {
  static char buf[2048];

  struct pollfd fd;
  fd.fd = (socket_t)this->sock;
  fd.events = POLLIN;
  if (poll(&fd, 1, timeout) <= 0) {
    return nullptr;
  }

  sockaddr sa;
  socklen_t salen = sizeof(struct sockaddr);
  len = (size_t)recvfrom(this->sock, buf, 2048, 0, &sa, &salen);
  addr.Set(sa);

  return buf;
}

}  // namespace inspector