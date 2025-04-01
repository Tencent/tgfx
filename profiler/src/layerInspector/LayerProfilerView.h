/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <QWidget>
#include <QDialog>
#include <QQmlApplicationEngine>
#include "socket/WebSocketServer.h"
#include "socket/TcpSocketClient.h"
#include "LayerTreeView.h"
#include "LayerAttributeView.h"
#include "LayerTreeModel.h"
#include "LayerAttributeModel.h"

class LayerProfilerView : public QWidget
{
  Q_OBJECT
public:
  LayerProfilerView(QString ip, quint16 port, QWidget* parent = nullptr);
  explicit LayerProfilerView(QWidget* parent = nullptr);
  ~LayerProfilerView();
  bool hasWebSocketConnection() const {
    if(m_WebSocketServer)
      return m_WebSocketServer->hasClientConnect();
    return false;
  }
  bool hasSocketConnection() const {
    if(m_TcpSocketClient)
      return m_TcpSocketClient->hasClientConnect();
    return false;
  }
  Q_INVOKABLE void SetHoveredSwitchState(bool state);
protected:
  void createMessage();
  void LayerProlfilerImplQML();
  void ProcessMessage(const QByteArray& message);
private:
  WebSocketServer* m_WebSocketServer;
  TcpSocketClient* m_TcpSocketClient;
  QDialog* m_ConnectBox = nullptr;
  LayerTreeView* m_LayerTreeView;
  LayerAttributeView* m_LayerAttributeView;
  QQmlApplicationEngine* m_LayerTreeEngine;
  QQmlApplicationEngine* m_LayerAttributeEngine;
  LayerTreeModel* m_LayerTreeModel;
  LayerAttributeModel* m_LayerAttributeModel;
};
