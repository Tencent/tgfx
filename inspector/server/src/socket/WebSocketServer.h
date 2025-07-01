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

#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

namespace inspector {
class WebSocketServer : public QObject {
  Q_OBJECT
 public:
  WebSocketServer(quint16 port, QObject* parent = nullptr);
  bool hasClientConnect() const {
    return m_HasClientConnect;
  }
  void close();
  void listen();
  void SendData(const QByteArray& data);
 Q_SIGNALS:
  void ClientConnected();
  void ClientBinaryData(const QByteArray& message);
  void ClientTextData(const QString& message);
  void ClientDisconnected();
 private slots:
  void onNewConnection();
  void onTextMessageReceived(const QString& message);
  void onBinaryMessageReceived(const QByteArray& message);
  void onClientDisconnected();

 private:
  QWebSocketServer* m_server;
  QWebSocket* m_ClientSocket;
  quint16 m_port;
  bool m_HasClientConnect = false;
};
}  // namespace inspector
