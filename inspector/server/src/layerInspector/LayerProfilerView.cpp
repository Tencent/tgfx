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
#include "LayerProfilerView.h"
#include <QLabel>
#include <QQmlContext>
#include <QQuickWindow>
#include <QHBoxLayout>


LayerProfilerView::LayerProfilerView(QString ip, quint16 port, QWidget* parent)
  :QWidget(parent),
  m_WebSocketServer(nullptr),
  m_TcpSocketClient(new TcpSocketClient(this, ip, port)),
  m_LayerTreeModel(new LayerTreeModel(this)),
  m_LayerAttributeModel(new LayerAttributeModel(this))
{
  LayerProlfilerQMLImpl();
  connect(m_TcpSocketClient, &TcpSocketClient::ServerBinaryData, this,
    &LayerProfilerView::ProcessMessage);

  connect(m_LayerTreeModel, &LayerTreeModel::selectAddress, [this](uint64_t address) {
    processSelectedLayer(address);
  });

  connect(m_LayerTreeModel, &LayerTreeModel::hoveredAddress, [this](uint64_t address) {
    auto data = feedBackData("HoverLayerAddress", address);
    m_TcpSocketClient->sendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::expandSubAttributeSignal, [this](uint64_t id) {
    auto data = feedBackData("SerializeSubAttribute", id);
    m_TcpSocketClient->sendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushLayerAttribute, [this](uint64_t address) {
    auto data = feedBackData("FlushAttribute", address);
    m_TcpSocketClient->sendData(data);
  });

  connect(m_LayerTreeModel, &LayerTreeModel::flushLayerTreeSignal, [this]() {
    auto data = feedBackData("FlushLayerTree", UINT64_MAX);
    m_TcpSocketClient->sendData(data);
  });
}

LayerProfilerView::LayerProfilerView(QWidget* parent)
  :QWidget(parent),
  m_WebSocketServer(new WebSocketServer(8085)),
  m_LayerTreeModel(new LayerTreeModel(this)),
  m_LayerAttributeModel(new LayerAttributeModel(this))
{
  LayerProlfilerQMLImpl();
  connect(m_WebSocketServer, &WebSocketServer::ClientBinaryData, this,
    &LayerProfilerView::ProcessMessage);
  connect(m_LayerTreeModel, &LayerTreeModel::selectAddress, [this](uint64_t address) {
    processSelectedLayer(address);
  });

  connect(m_LayerTreeModel, &LayerTreeModel::hoveredAddress, [this](uint64_t address) {
    auto data = feedBackData("HoverLayerAddress", address);
    m_WebSocketServer->SendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::expandSubAttributeSignal, [this](uint64_t id) {
    auto data = feedBackData("SerializeSubAttribute", id);
    m_WebSocketServer->SendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushLayerAttribute, [this](uint64_t address) {
    auto data = feedBackData("FlushAttribute", address);
    m_WebSocketServer->SendData(data);
  });

  connect(m_LayerTreeModel, &LayerTreeModel::flushLayerTreeSignal, [this]() {
    auto data = feedBackData("FlushLayerTree", UINT64_MAX);
    m_WebSocketServer->SendData(data);
  });
}

LayerProfilerView::~LayerProfilerView() {
  if(m_WebSocketServer)
    m_WebSocketServer->close();
}

void LayerProfilerView::SetHoveredSwitchState(bool state) {
  auto data = feedBackData("EnalbeLayerInspect", state);
  if(m_WebSocketServer)
    m_WebSocketServer->SendData(data);
  if(m_TcpSocketClient)
    m_TcpSocketClient->sendData(data);
}

void LayerProfilerView::flushAttribute() {
  m_LayerAttributeModel->flushTree();
}

void LayerProfilerView::flushLayerTree() {
  m_LayerTreeModel->flushLayerTree();
}

void LayerProfilerView::LayerProlfilerQMLImpl() {
  setFixedSize(1920, 1080);
  auto layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_LayerTreeEngine = new QQmlApplicationEngine();
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerTreeModel", m_LayerTreeModel);
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerProfileView", this);
  m_LayerTreeEngine->load(QUrl(QStringLiteral("qrc:/qml/layerInspector/LayerTree.qml")));
  auto quickWindow = static_cast<QQuickWindow*>(m_LayerTreeEngine->rootObjects().first());
  auto layerTreeWidget = createWindowContainer(quickWindow);
  layerTreeWidget->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);

  m_LayerAttributeEngine = new QQmlApplicationEngine();
  m_LayerAttributeEngine->rootContext()->setContextProperty("_layerAttributeModel", m_LayerAttributeModel);
  m_LayerAttributeEngine->load(QUrl(QStringLiteral("qrc:/qml/layerInspector/LayerAttribute.qml")));
  auto quickWindow1 = static_cast<QQuickWindow*>(m_LayerAttributeEngine->rootObjects().first());
  auto layerAttributeWidget = createWindowContainer(quickWindow1);
  layerAttributeWidget->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);

  layout->addWidget(layerTreeWidget);
  layout->addWidget(layerAttributeWidget);
  layout->setSpacing(0);
}

void LayerProfilerView::ProcessMessage(const QByteArray& message) {
  auto ptr = message.data();
  auto map = flexbuffers::GetRoot((const uint8_t*)ptr, (size_t)message.size()).AsMap();
  std::string type = map["Type"].AsString().str();
  auto contentMap = map["Content"].AsMap();
  if(type == "LayerTree") {
    m_LayerTreeModel->setLayerTreeData(contentMap);
    auto currentAddress = m_LayerAttributeModel->GetCurrentAddress();
    if(!m_LayerTreeModel->selectLayer(currentAddress)) {
      m_LayerAttributeModel->clearAttribute();
    }
  }else if(type == "LayerAttribute") {
    m_LayerAttributeModel->setLayerAttribute(contentMap);
  }else if(type == "LayerSubAttribute") {
    m_LayerAttributeModel->setLayerSubAttribute(contentMap);
  }else if(type == "PickedLayerAddress") {
    auto address = contentMap["Address"].AsUInt64();
    processSelectedLayer(address);
    m_LayerTreeModel->selectLayer(address);
  }else if(type == "FlushAttributeAck") {
    auto address = contentMap["Address"].AsUInt64();
    processSelectedLayer(address);
  }else {
    qDebug() << "Unknown message type!";
  }
}

QByteArray LayerProfilerView::feedBackData(const std::string &type, uint64_t value) {
  flexbuffers::Builder fbb;
  auto mapStart = fbb.StartMap();
  fbb.Key("Type");
  fbb.String(type);
  fbb.Key("Value");
  fbb.UInt(value);
  fbb.EndMap(mapStart);
  fbb.Finish();
  return QByteArray((char*)fbb.GetBuffer().data(), (qsizetype)fbb.GetBuffer().size());
}

void LayerProfilerView::sendSelectedAddress(uint64_t address) {
  auto data = feedBackData("SelectedLayerAddress", address);
  if(m_WebSocketServer)
    m_WebSocketServer->SendData(data);
  if(m_TcpSocketClient)
    m_TcpSocketClient->sendData(data);
}

void LayerProfilerView::sendSerializeAttributeAddress(uint64_t address) {
  auto data = feedBackData("SerializeAttribute", address);
  if(m_WebSocketServer)
    m_WebSocketServer->SendData(data);
  if(m_TcpSocketClient)
    m_TcpSocketClient->sendData(data);
}

void LayerProfilerView::processSelectedLayer(uint64_t address) {
  sendSelectedAddress(address);
  m_LayerAttributeModel->SetCurrentAddress(address);
  if(m_LayerAttributeModel->isExistedInLayerMap(address)) {
    m_LayerAttributeModel->switchToLayer(address);
  }else {
    sendSerializeAttributeAddress(address);
  }
}
