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

#include "TcpSocketClient.h"
#include <utility>

namespace inspector {
TcpSocketClient::TcpSocketClient(QObject* parent, QString ip, quint16 port)
    : QObject(parent), m_IsConnection(false), data(), currentIndex(0), size(0), Remainder(0) {
  m_TcpSocket = new QTcpSocket(this);

  connect(m_TcpSocket, &QTcpSocket::connected, this, &TcpSocketClient::onSocketConnected);
  connect(m_TcpSocket, &QTcpSocket::disconnected, this, &TcpSocketClient::onSocketDisconnected);
  connect(m_TcpSocket, &QTcpSocket::readyRead, this, &TcpSocketClient::onSocketReadyRead);
  connect(m_TcpSocket, &QTcpSocket::errorOccurred, this, &TcpSocketClient::onSocketErrorOccurred);

  connection(std::move(ip), port);
}

TcpSocketClient::~TcpSocketClient() {
}
void TcpSocketClient::connection(QString ip, quint16 port) {
  if (!m_IsConnection) {
    m_TcpSocket->connectToHost(ip, port);
  }
}

void TcpSocketClient::disConnection() {
  if (m_IsConnection) {
    m_TcpSocket->disconnectFromHost();
  }
}

void TcpSocketClient::sendData(const QByteArray& data) {
  if (!m_IsConnection) {
    qDebug() << "Server is not connected!\n";
    return;
  }
  int size = (int)data.size();
  m_TcpSocket->write((char*)&size, sizeof(int));
  m_TcpSocket->write(data);
}
void TcpSocketClient::onSocketConnected() {
  m_IsConnection = true;
}
void TcpSocketClient::onSocketDisconnected() {
  m_IsConnection = false;
}
void TcpSocketClient::onSocketReadyRead() {
  if (Remainder == 0) {
    m_TcpSocket->read((char*)&size, sizeof(int));
    Remainder = size;
    data.resize(size);
  }
  if (Remainder != 0) {
    auto readSize = m_TcpSocket->read(data.data() + currentIndex, Remainder);
    currentIndex += readSize;
    Remainder -= readSize;
    if (Remainder == 0) {
      emit ServerBinaryData(data);
      currentIndex = 0;
    }
  }
}
void TcpSocketClient::onSocketErrorOccurred(QAbstractSocket::SocketError error) {
  Q_UNUSED(error);
  qDebug() << "error: " << m_TcpSocket->errorString();
}
}  // namespace inspector