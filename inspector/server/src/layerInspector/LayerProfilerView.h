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

#include <QtGui/qwindow.h>
#include <kddockwidgets/qtquick/views/DockWidget.h>
#include <QDialog>
#include <QQmlApplicationEngine>
#include "LayerAttributeModel.h"
#include "LayerInspectorProtocol.h"
#include "LayerTreeModel.h"
#include "MemoryImageProvider.h"
#include "flatbuffers/flexbuffers.h"
#include "socket/TcpSocketClient.h"
#include "socket/WebSocketServer.h"

namespace inspector {
class LayerProfilerView : public QObject {
  Q_OBJECT
 public:
  LayerProfilerView(QString ip, quint16 port);
  LayerProfilerView();
  ~LayerProfilerView() override;
  bool hasWebSocketConnection() const {
    if (m_WebSocketServer) return m_WebSocketServer->hasClientConnect();
    return false;
  }
  bool hasSocketConnection() const {
    if (m_TcpSocketClient) return m_TcpSocketClient->hasClientConnect();
    return false;
  }
  Q_INVOKABLE void SetHoveredSwitchState(bool state);
  Q_INVOKABLE void flushAttribute();
  Q_INVOKABLE void flushLayerTree();
  Q_INVOKABLE void openStartView();
  Q_INVOKABLE void showLayerTree();
  Q_INVOKABLE void showLayerAttributeTree();
  void cleanView();
 signals:
  void viewHide();

 protected:
  void LayerProlfilerQMLImpl();
  void ProcessMessage(const QByteArray& message);

 private:
  QByteArray feedBackData(LayerInspectorMsgType type, uint64_t value);

  void sendSelectedAddress(uint64_t address);

  void sendSerializeAttributeAddress(uint64_t address);

  void processSelectedLayer(uint64_t address);

  void processImageFlush(uint64_t imageID);

 private:
  WebSocketServer* m_WebSocketServer;
  TcpSocketClient* m_TcpSocketClient;
  std::unique_ptr<QQmlApplicationEngine> m_LayerTreeEngine;
  MemoryImageProvider* imageProvider;
  LayerTreeModel* m_LayerTreeModel;
  LayerAttributeModel* m_LayerAttributeModel;
  KDDockWidgets::QtQuick::DockWidget* layerTree;
  KDDockWidgets::QtQuick::DockWidget* layerAttributeTree;
};
}  // namespace inspector