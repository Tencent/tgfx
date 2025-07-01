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

#include <QTcpSocket>

namespace inspector {
class TcpSocketClient : public QObject {
  Q_OBJECT
 public:
  TcpSocketClient(QObject* parent, QString ip, quint16 port);
  ~TcpSocketClient() override;
  void connection(QString ip, quint16 port);
  void disConnection();
  void sendData(const QByteArray& data);
  bool hasClientConnect() const {
    return m_IsConnection;
  }
 Q_SIGNALS:
  void ServerBinaryData(const QByteArray& message);
 private slots:
  void onSocketConnected();
  void onSocketDisconnected();
  void onSocketReadyRead();
  void onSocketErrorOccurred(QAbstractSocket::SocketError error);

 private:
  bool m_IsConnection;
  QTcpSocket* m_TcpSocket;
  QByteArray data;
  int currentIndex;
  int size;
  int Remainder;
};
}  // namespace inspector
