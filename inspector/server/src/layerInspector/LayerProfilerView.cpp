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
#include "LayerProfilerView.h"
#include "StartView.h"
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtquick/views/Group.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QQmlContext>
#include <QQuickWindow>

class LayerProfilerViewFactory : public KDDockWidgets::QtQuick::ViewFactory {
public:
  ~LayerProfilerViewFactory() override = default;

  QUrl tabbarFilename() const override {
    return QUrl("qrc:/qml/TabBar.qml");
  }

  QUrl separatorFilename() const override {
    return QUrl("qrc:/qml/Separator2.qml");
  }

  QUrl titleBarFilename() const override {
    return QUrl("qrc:/qml/layerInspector/LayerProfilerTitleBar.qml");
  }

  QUrl groupFilename() const override {
    return QUrl("qrc:/qml/layerInspector/LayerInspectorGroup.qml");
  }
};

LayerProfilerView::LayerProfilerView(QString ip, quint16 port)
    : QObject(nullptr), m_WebSocketServer(nullptr),
      m_TcpSocketClient(new TcpSocketClient(this, ip, port)),
      m_LayerTreeModel(new LayerTreeModel(this)),
      m_LayerAttributeModel(new LayerAttributeModel(this)) {
  LayerProlfilerQMLImpl();
  connect(m_TcpSocketClient, &TcpSocketClient::ServerBinaryData, this,
          &LayerProfilerView::ProcessMessage);

  connect(m_LayerTreeModel, &LayerTreeModel::selectAddress,
          [this](uint64_t address) { processSelectedLayer(address); });

  connect(m_LayerTreeModel, &LayerTreeModel::hoveredAddress, [this](uint64_t address) {
    auto data = feedBackData("HoverLayerAddress", address);
    m_TcpSocketClient->sendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::expandSubAttributeSignal,
          [this](uint64_t id) {
            auto data = feedBackData("SerializeSubAttribute", id);
            m_TcpSocketClient->sendData(data);
          });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushLayerAttribute,
          [this](uint64_t address) {
            auto data = feedBackData("FlushAttribute", address);
            m_TcpSocketClient->sendData(data);
          });

  connect(m_LayerTreeModel, &LayerTreeModel::flushLayerTreeSignal, [this]() {
    auto data = feedBackData("FlushLayerTree", UINT64_MAX);
    m_TcpSocketClient->sendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushImageChild, [this](uint64_t imageID) {
    processImageFlush(imageID);
  });
}

LayerProfilerView::LayerProfilerView()
    : QObject(nullptr), m_WebSocketServer(new WebSocketServer(8085)),
      m_LayerTreeModel(new LayerTreeModel(this)),
      m_LayerAttributeModel(new LayerAttributeModel(this)) {
  LayerProlfilerQMLImpl();
  connect(m_WebSocketServer, &WebSocketServer::ClientBinaryData, this,
          &LayerProfilerView::ProcessMessage);
  connect(m_LayerTreeModel, &LayerTreeModel::selectAddress,
          [this](uint64_t address) { processSelectedLayer(address); });

  connect(m_LayerTreeModel, &LayerTreeModel::hoveredAddress, [this](uint64_t address) {
    auto data = feedBackData("HoverLayerAddress", address);
    m_WebSocketServer->SendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::expandSubAttributeSignal,
          [this](uint64_t id) {
            auto data = feedBackData("SerializeSubAttribute", id);
            m_WebSocketServer->SendData(data);
          });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushLayerAttribute,
          [this](uint64_t address) {
            auto data = feedBackData("FlushAttribute", address);
            m_WebSocketServer->SendData(data);
          });

  connect(m_LayerTreeModel, &LayerTreeModel::flushLayerTreeSignal, [this]() {
    auto data = feedBackData("FlushLayerTree", UINT64_MAX);
    m_WebSocketServer->SendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushImageChild, [this](uint64_t imageID) {
    processImageFlush(imageID);
  });
}

LayerProfilerView::~LayerProfilerView() {
  if (m_WebSocketServer) m_WebSocketServer->close();
}

void LayerProfilerView::SetHoveredSwitchState(bool state) {
  auto data = feedBackData("EnalbeLayerInspect", state);
  if (m_WebSocketServer) m_WebSocketServer->SendData(data);
  if (m_TcpSocketClient) m_TcpSocketClient->sendData(data);
}

void LayerProfilerView::flushAttribute() {
  m_LayerAttributeModel->flushTree();
}

void LayerProfilerView::flushLayerTree() {
  m_LayerTreeModel->flushLayerTree();
}

void LayerProfilerView::openStartView() {
  cleanView();
  auto startView = new inspector::StartView(this);
  startView->showStartView();
}

void LayerProfilerView::cleanView() {
    if (m_LayerTreeEngine) {
      m_LayerTreeEngine->deleteLater();
      m_LayerTreeEngine = nullptr;
    }
}

void LayerProfilerView::LayerProlfilerQMLImpl() {
  qmlRegisterUncreatableType<KDDockWidgets::QtQuick::Group>("com.kdab.dockwidgets", 2, 0,
                                           "GroupView", QStringLiteral("Internal usage only"));
  KDDockWidgets::Config::self().setViewFactory(new LayerProfilerViewFactory);
  auto func = [](KDDockWidgets::DropLocation loc,
                const KDDockWidgets::Core::DockWidget::List &source,
                const KDDockWidgets::Core::DockWidget::List &target,
                KDDockWidgets::Core::DropArea *) {
    KDDW_UNUSED(target);
    bool isDraggingRenderTree =
      std::find_if(source.cbegin(), source.cend(),
      [](KDDockWidgets::Core::DockWidget *dw) {
            return dw->uniqueName() == QLatin1String("RenderTree");
          })
      != source.cend();
    bool isDraggingAttribute = std::find_if(source.cbegin(), source.cend(),
      [](KDDockWidgets::Core::DockWidget *dw) {
            return dw->uniqueName() == QLatin1String("Attribute");
          })
      != source.cend();
    return (loc & (KDDockWidgets::DropLocation_Inner | KDDockWidgets::DropLocation_Outter)) || !(isDraggingRenderTree||isDraggingAttribute);
  };
  KDDockWidgets::Config::self().setDropIndicatorAllowedFunc(func);
  m_LayerTreeEngine = new QQmlApplicationEngine();
  imageProvider = new MemoryImageProvider();
  m_LayerTreeEngine->addImageProvider(QLatin1String("RenderableImage"), imageProvider);

  m_LayerTreeEngine->rootContext()->setContextProperty("_layerAttributeModel",
                                                            m_LayerAttributeModel);
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerTreeModel", m_LayerTreeModel);
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerProfileView", this);
  m_LayerTreeEngine->rootContext()->setContextProperty("imageProvider", imageProvider);
  KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(m_LayerTreeEngine);
  m_LayerTreeEngine->load("qrc:/qml/layerInspector/LayerProfilerView.qml");

  if (m_LayerTreeEngine->rootObjects().isEmpty()) {
    qWarning() << "Failed to load LayerProfilerView.qml";
    return;
  }

  auto window = qobject_cast<QWindow*>(m_LayerTreeEngine->rootObjects().first());
  if (window) {
    window->show();
  }

}

void LayerProfilerView::ProcessMessage(const QByteArray& message) {
  auto ptr = message.data();
  auto map = flexbuffers::GetRoot((const uint8_t*)ptr, (size_t)message.size()).AsMap();
  std::string type = map["Type"].AsString().str();
  auto contentMap = map["Content"].AsMap();
  if (type == "LayerTree") {
    m_LayerTreeModel->setLayerTreeData(contentMap);
    auto currentAddress = m_LayerAttributeModel->GetCurrentAddress();
    if (!m_LayerTreeModel->selectLayer(currentAddress)) {
      m_LayerAttributeModel->clearAttribute();
    }
  } else if (type == "LayerAttribute") {
    m_LayerAttributeModel->setLayerAttribute(contentMap);
  } else if (type == "LayerSubAttribute") {
    m_LayerAttributeModel->setLayerSubAttribute(contentMap);
  } else if (type == "PickedLayerAddress") {
    auto address = contentMap["Address"].AsUInt64();
    processSelectedLayer(address);
    m_LayerTreeModel->selectLayer(address);
  } else if (type == "FlushAttributeAck") {
    auto address = contentMap["Address"].AsUInt64();
    processSelectedLayer(address);
  }else if(type == "ImageData") {
    int width = contentMap["width"].AsInt32();
    int height = contentMap["height"].AsInt32();
    auto blob = contentMap["data"].AsBlob();
    QByteArray data((char*)blob.data(), (qsizetype)blob.size());
    imageProvider->setImage(imageProvider->ImageID(), width, height, data);
  } else {
    qDebug() << "Unknown message type!";
  }
}

QByteArray LayerProfilerView::feedBackData(const std::string& type, uint64_t value) {
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
  if (m_WebSocketServer) m_WebSocketServer->SendData(data);
  if (m_TcpSocketClient) m_TcpSocketClient->sendData(data);
}

void LayerProfilerView::sendSerializeAttributeAddress(uint64_t address) {
  auto data = feedBackData("SerializeAttribute", address);
  if (m_WebSocketServer) m_WebSocketServer->SendData(data);
  if (m_TcpSocketClient) m_TcpSocketClient->sendData(data);
}

void LayerProfilerView::processSelectedLayer(uint64_t address) {
  sendSelectedAddress(address);
  m_LayerAttributeModel->SetCurrentAddress(address);
  if (m_LayerAttributeModel->isExistedInLayerMap(address)) {
    m_LayerAttributeModel->switchToLayer(address);
  } else {
    sendSerializeAttributeAddress(address);
  }
}

void LayerProfilerView::processImageFlush(uint64_t imageID) {
  if(!imageProvider->isImageExisted(imageID)) {
    imageProvider->setCurrentImageID(imageID);
    auto data = feedBackData("FlushImage", imageID);
    if (m_WebSocketServer) m_WebSocketServer->SendData(data);
    if (m_TcpSocketClient) m_TcpSocketClient->sendData(data);
  }
}
