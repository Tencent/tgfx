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

#include "WebSocketServer.h"

namespace inspector {
WebSocketServer::WebSocketServer(quint16 port, QObject* parent) : QObject(parent), m_port(port) {
  // 创建 WebSocket 服务器
  m_server = new QWebSocketServer("My WebSocket Server", QWebSocketServer::NonSecureMode, this);

  // 监听客户端连接
  if (m_server->listen(QHostAddress::Any, m_port)) {
    qDebug() << "WebSocket server is listening on port" << m_port;
    connect(m_server, &QWebSocketServer::newConnection, this, &WebSocketServer::onNewConnection);
  } else {
    qCritical() << "Failed to start WebSocket server on port" << m_port;
  }
}

void WebSocketServer::close() {
  m_server->close();
}

void WebSocketServer::listen() {
  m_server->listen();
}

void WebSocketServer::SendData(const QByteArray& data) {
  if (m_ClientSocket) {
    m_ClientSocket->sendBinaryMessage(data);
  }
}

void WebSocketServer::onNewConnection() {
  // 获取客户端连接的 WebSocket 对象
  m_ClientSocket = m_server->nextPendingConnection();

  // 处理客户端文本消息
  connect(m_ClientSocket, &QWebSocket::textMessageReceived, this,
          &WebSocketServer::onTextMessageReceived);

  // 处理客户端二进制消息
  connect(m_ClientSocket, &QWebSocket::binaryMessageReceived, this,
          &WebSocketServer::onBinaryMessageReceived);

  // 处理客户端断开连接
  connect(m_ClientSocket, &QWebSocket::disconnected, this, &WebSocketServer::onClientDisconnected);

  qDebug() << "New client connected:" << m_ClientSocket->peerAddress().toString();
  m_HasClientConnect = true;
  emit ClientConnected();
}

void WebSocketServer::onTextMessageReceived(const QString& message) {
  // 获取发送消息的客户端
  QWebSocket* clientSocket = qobject_cast<QWebSocket*>(sender());

  if (clientSocket) {
    qDebug() << "Received text message from client:" << message;
    emit ClientTextData(message);
  }
}

void WebSocketServer::onBinaryMessageReceived(const QByteArray& message) {
  // 获取发送消息的客户端
  QWebSocket* clientSocket = qobject_cast<QWebSocket*>(sender());

  if (clientSocket) {
    qDebug() << "Received binary message from client, size:" << message.size();
    emit ClientBinaryData(message);
  }
}

void WebSocketServer::onClientDisconnected() {
  // 获取断开连接的客户端
  QWebSocket* clientSocket = qobject_cast<QWebSocket*>(sender());

  if (clientSocket) {
    qDebug() << "Client disconnected:" << clientSocket->peerAddress().toString();
    clientSocket->deleteLater();
    emit ClientDisconnected();
  }
}
}  // namespace inspector