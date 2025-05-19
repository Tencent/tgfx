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
#include "FeedbackInterface.h"
#include "generate/SerializationStructure_generated.h"
#include <QLabel>
#include <QDialog>
#include <QLayout>
#include <QByteArray>
#include <QQmlContext>
#include <QQuickWindow>


using namespace tgfx::fbs;

LayerProfilerView::LayerProfilerView(QString ip, quint16 port, QWidget* parent)
  :QWidget(parent),
  m_WebSocketServer(nullptr),
  m_TcpSocketClient(new TcpSocketClient(this, ip, port)),
  m_LayerTreeModel(new LayerTreeModel(this)),
  m_LayerAttributeModel(new LayerAttributeModel(this))
{
  LayerProlfilerImplQML();
  connect(m_TcpSocketClient, &TcpSocketClient::ServerBinaryData, this,
    &LayerProfilerView::ProcessMessage);

  connect(m_LayerTreeModel, &LayerTreeModel::selectAddress, [this](uint64_t address) {
    profiler::FeedbackData data = {};
    data.type = profiler::SelectedLayerAddress;
    data.address = address;
    m_TcpSocketClient->sendData(QByteArray((char*)&data, sizeof(data)));
  });

  connect(m_LayerTreeModel, &LayerTreeModel::hoveredAddress, [this](uint64_t address) {
    profiler::FeedbackData data = {};
    data.type = profiler::HoverLayerAddress;
    data.address = address;
    m_TcpSocketClient->sendData(QByteArray((char*)&data, sizeof(data)));
  });

}

LayerProfilerView::LayerProfilerView(QWidget* parent)
  :QWidget(parent),
  m_WebSocketServer(new WebSocketServer(8085)),
  m_LayerTreeModel(new LayerTreeModel(this)),
  m_LayerAttributeModel(new LayerAttributeModel(this))
{
  LayerProlfilerImplQML();
  connect(m_WebSocketServer, &WebSocketServer::ClientBinaryData, this,
    &LayerProfilerView::ProcessMessage);
  connect(m_LayerTreeModel, &LayerTreeModel::selectAddress, [this](uint64_t address) {
    profiler::FeedbackData data = {};
    data.type = profiler::SelectedLayerAddress;
    data.address = address;
    m_WebSocketServer->SendData(QByteArray((char*)&data, sizeof(data)));
  });

  connect(m_LayerTreeModel, &LayerTreeModel::hoveredAddress, [this](uint64_t address) {
    profiler::FeedbackData data = {};
    data.type = profiler::HoverLayerAddress;
    data.address = address;
    m_WebSocketServer->SendData(QByteArray((char*)&data, sizeof(data)));
  });
}

LayerProfilerView::~LayerProfilerView() {
  if(m_WebSocketServer)
    m_WebSocketServer->close();
}

void LayerProfilerView::SetHoveredSwitchState(bool state) {
  profiler::FeedbackData data = {};
  data.type = profiler::EnalbeLayerInspect;
  data.address = state ? 1 : 0;
  if(m_WebSocketServer)
    m_WebSocketServer->SendData(QByteArray((char*)&data, sizeof(data)));
  if(m_TcpSocketClient)
    m_TcpSocketClient->sendData(QByteArray((char*)&data, sizeof(data)));
}

void LayerProfilerView::createMessage() {
  m_ConnectBox = new QDialog;
  auto layout = new QVBoxLayout(m_ConnectBox);
  auto textLable = new QLabel(m_ConnectBox);
  textLable->setAlignment(Qt::AlignCenter);
  textLable->setText("Waiting for connect...");
  layout->addWidget(textLable);
  connect(m_WebSocketServer, &WebSocketServer::ClientConnected, [this]() {
    m_ConnectBox->close();
  });
  m_ConnectBox->exec();
}

void LayerProfilerView::LayerProlfilerImplQML() {
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_LayerTreeEngine = new QQmlApplicationEngine();
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerTreeModel", m_LayerTreeModel);
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerProfileView", this);
  m_LayerTreeEngine->load(QUrl(QStringLiteral("qrc:/qml/LayerTree.qml")));
  auto quickWindow = static_cast<QQuickWindow*>(m_LayerTreeEngine->rootObjects().first());
  auto layerTreeWidget = createWindowContainer(quickWindow);
  layerTreeWidget->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);

  m_LayerAttributeEngine = new QQmlApplicationEngine();
  m_LayerAttributeEngine->rootContext()->setContextProperty("_layerAttributeModel", m_LayerAttributeModel);
  m_LayerAttributeEngine->load(QUrl(QStringLiteral("qrc:/qml/LayerAttribute.qml")));
  auto quickWindow1 = static_cast<QQuickWindow*>(m_LayerAttributeEngine->rootObjects().first());
  auto layerAttributeWidget = createWindowContainer(quickWindow1);
  layerAttributeWidget->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);

  layout->addWidget(layerTreeWidget);
  layout->addWidget(layerAttributeWidget);
  layout->setSpacing(0);
}

void LayerProfilerView::ProcessMessage(const QByteArray& message) {
  qDebug() << "Message size: " << message.size();
  auto ptr = message.data();
  auto map = flexbuffers::GetRoot((const uint8_t*)ptr, (size_t)message.size()).AsMap();
  std::string type = map["Type"].AsString().str();
  auto contentMap = map["Content"].AsMap();
  if(type == "LayerTree") {
    m_LayerTreeModel->setLayerTreeData(contentMap);
  }else if(type == "LayerAttribute") {
    m_LayerAttributeModel->setLayerAttribute(contentMap);
    m_LayerTreeModel->expandSelectedLayer(m_LayerAttributeModel->GetSelectedAddress());
  }else {
    qDebug() << "Unknown message type!";
  }
  // switch(type) {
  //   case Type::Type_TreeData: {
  //     const TreeNode* treeNode = FinalData->data_as_TreeNode();
  //     m_LayerTreeModel->setLayerTreeData(treeNode);
  //     break;
  //   }
  //   case Type::Type_LayerData: {
  //     const Layer* layer = FinalData->data_as_Layer();
  //     m_LayerAttributeModel->ProcessMessage(layer);
  //     m_LayerTreeModel->expandSelectedLayer(m_LayerAttributeModel->GetSelectedAddress());
  //     break;
  //   }
  //   default:
  //     qDebug() << "Unknown message type!";
  // }
}