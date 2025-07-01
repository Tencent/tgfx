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
#include <kddockwidgets/Config.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtquick/Platform.h>
#include <kddockwidgets/qtquick/ViewFactory.h>
#include <kddockwidgets/qtquick/views/Group.h>
#include <kddockwidgets/qtquick/views/MainWindow.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QQmlContext>
#include <QQuickWindow>
#include "StartView.h"

namespace inspector {
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
    auto data = feedBackData(LayerInspectorMsgType::HoverLayerAddress, address);
    m_TcpSocketClient->sendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::expandSubAttributeSignal,
          [this](uint64_t id) {
            auto data = feedBackData(LayerInspectorMsgType::SerializeSubAttribute, id);
            m_TcpSocketClient->sendData(data);
          });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushLayerAttribute,
          [this](uint64_t address) {
            auto data = feedBackData(LayerInspectorMsgType::FlushAttribute, address);
            m_TcpSocketClient->sendData(data);
          });

  connect(m_LayerTreeModel, &LayerTreeModel::flushLayerTreeSignal, [this]() {
    auto data = feedBackData(LayerInspectorMsgType::FlushLayerTree, UINT64_MAX);
    m_TcpSocketClient->sendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushImageChild,
          [this](uint64_t imageID) { processImageFlush(imageID); });
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
    auto data = feedBackData(LayerInspectorMsgType::HoverLayerAddress, address);
    m_WebSocketServer->SendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::expandSubAttributeSignal,
          [this](uint64_t id) {
            auto data = feedBackData(LayerInspectorMsgType::SerializeSubAttribute, id);
            m_WebSocketServer->SendData(data);
          });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushLayerAttribute,
          [this](uint64_t address) {
            auto data = feedBackData(LayerInspectorMsgType::FlushAttribute, address);
            m_WebSocketServer->SendData(data);
          });

  connect(m_LayerTreeModel, &LayerTreeModel::flushLayerTreeSignal, [this]() {
    auto data = feedBackData(LayerInspectorMsgType::FlushLayerTree, UINT64_MAX);
    m_WebSocketServer->SendData(data);
  });

  connect(m_LayerAttributeModel, &LayerAttributeModel::flushImageChild,
          [this](uint64_t imageID) { processImageFlush(imageID); });
}

LayerProfilerView::~LayerProfilerView() {
  if (m_WebSocketServer) {
    m_WebSocketServer->close();
  }
  if (m_TcpSocketClient) {
    m_TcpSocketClient->disConnection();
  }
  if (layerTree) {
    layerTree->deleteLater();
  }
  if (layerAttributeTree) {
    layerAttributeTree->deleteLater();
  }
}

void LayerProfilerView::SetHoveredSwitchState(bool state) {
  auto data = feedBackData(LayerInspectorMsgType::EnableLayerInspector, state);
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

void LayerProfilerView::showLayerTree() {
  layerTree->show();
}

void LayerProfilerView::showLayerAttributeTree() {
  layerAttributeTree->show();
}

void LayerProfilerView::cleanView() {
  if (m_LayerTreeEngine) {
    m_LayerTreeEngine->deleteLater();
    m_LayerTreeEngine = nullptr;
  }
}

void LayerProfilerView::LayerProlfilerQMLImpl() {
  qmlRegisterUncreatableType<KDDockWidgets::QtQuick::Group>(
      "com.kdab.dockwidgets", 2, 0, "GroupView", QStringLiteral("Internal usage only"));
  KDDockWidgets::Config::self().setViewFactory(new LayerProfilerViewFactory);
  auto func =
      [](KDDockWidgets::DropLocation loc, const KDDockWidgets::Core::DockWidget::List& source,
         const KDDockWidgets::Core::DockWidget::List& target, KDDockWidgets::Core::DropArea*) {
        KDDW_UNUSED(target);
        bool isDraggingRenderTree =
            std::find_if(source.cbegin(), source.cend(), [](KDDockWidgets::Core::DockWidget* dw) {
              return dw->uniqueName() == QLatin1String("RenderTree");
            }) != source.cend();
        bool isDraggingAttribute =
            std::find_if(source.cbegin(), source.cend(), [](KDDockWidgets::Core::DockWidget* dw) {
              return dw->uniqueName() == QLatin1String("Attribute");
            }) != source.cend();
        return (loc & (KDDockWidgets::DropLocation_Inner | KDDockWidgets::DropLocation_Outter)) ||
               !(isDraggingRenderTree || isDraggingAttribute);
      };
  KDDockWidgets::Config::self().setDropIndicatorAllowedFunc(func);
  m_LayerTreeEngine = std::make_unique<QQmlApplicationEngine>();
  imageProvider = new MemoryImageProvider();
  m_LayerTreeEngine->addImageProvider(QLatin1String("RenderableImage"), imageProvider);

  m_LayerTreeEngine->rootContext()->setContextProperty("_layerAttributeModel",
                                                       m_LayerAttributeModel);
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerTreeModel", m_LayerTreeModel);
  m_LayerTreeEngine->rootContext()->setContextProperty("_layerProfileView", this);
  m_LayerTreeEngine->rootContext()->setContextProperty("imageProvider", imageProvider);
  KDDockWidgets::QtQuick::Platform::instance()->setQmlEngine(m_LayerTreeEngine.get());
  m_LayerTreeEngine->load("qrc:/qml/layerInspector/LayerProfilerView.qml");
  layerTree = new KDDockWidgets::QtQuick::DockWidget("RenderTree");
  layerTree->setGuestItem(QStringLiteral("qrc:/qml/layerInspector/LayerTree.qml"));

  layerAttributeTree = new KDDockWidgets::QtQuick::DockWidget("Attribute");
  layerAttributeTree->setGuestItem(QStringLiteral("qrc:/qml/layerInspector/LayerAttribute.qml"));

  auto mainArea = KDDockWidgets::DockRegistry::self()->mainDockingAreas().constFirst();
  mainArea->addDockWidget(layerTree, KDDockWidgets::Location_OnLeft);
  mainArea->addDockWidget(layerAttributeTree, KDDockWidgets::Location_OnRight);

  if (m_LayerTreeEngine->rootObjects().isEmpty()) {
    qWarning() << "Failed to load LayerProfilerView.qml";
    return;
  }

  auto window = qobject_cast<QWindow*>(m_LayerTreeEngine->rootObjects().first());
  if (window) {
    window->show();
    connect(window, &QWindow::visibilityChanged, [this](QWindow::Visibility visibility) {
      if (visibility == QWindow::Visibility::Hidden) {
        m_TcpSocketClient->disConnection();
        emit viewHide();
      }
    });
  }
}

void LayerProfilerView::ProcessMessage(const QByteArray& message) {
  auto ptr = message.data();
  auto map = flexbuffers::GetRoot((const uint8_t*)ptr, (size_t)message.size()).AsMap();
  auto type = static_cast<LayerInspectorMsgType>(map["Type"].AsUInt8());
  auto contentMap = map["Content"].AsMap();
  switch (type) {
    case LayerInspectorMsgType::LayerTree: {
      m_LayerTreeModel->setLayerTreeData(contentMap);
      auto currentAddress = m_LayerAttributeModel->GetCurrentAddress();
      if (!m_LayerTreeModel->selectLayer(currentAddress)) {
        m_LayerAttributeModel->clearAttribute();
      }
      break;
    }
    case LayerInspectorMsgType::LayerAttribute: {
      m_LayerAttributeModel->setLayerAttribute(contentMap);
      break;
    }
    case LayerInspectorMsgType::LayerSubAttribute: {
      m_LayerAttributeModel->setLayerSubAttribute(contentMap);
      break;
    }
    case LayerInspectorMsgType::PickedLayerAddress: {
      auto address = contentMap["Address"].AsUInt64();
      processSelectedLayer(address);
      m_LayerTreeModel->selectLayer(address);
      break;
    }
    case LayerInspectorMsgType::FlushAttributeAck: {
      auto address = contentMap["Address"].AsUInt64();
      processSelectedLayer(address);
      break;
    }
    case LayerInspectorMsgType::ImageData: {
      int width = contentMap["width"].AsInt32();
      int height = contentMap["height"].AsInt32();
      auto blob = contentMap["data"].AsBlob();
      QByteArray data((char*)blob.data(), (qsizetype)blob.size());
      imageProvider->setImage(imageProvider->ImageID(), width, height, data);
      break;
    }
    default: {
      qDebug() << "Unknown message type!";
    }
  }
}

QByteArray LayerProfilerView::feedBackData(LayerInspectorMsgType type, uint64_t value) {
  flexbuffers::Builder fbb;
  auto mapStart = fbb.StartMap();
  fbb.Key("Type");
  fbb.UInt(static_cast<uint8_t>(type));
  fbb.Key("Value");
  fbb.UInt(value);
  fbb.EndMap(mapStart);
  fbb.Finish();
  return QByteArray((char*)fbb.GetBuffer().data(), (qsizetype)fbb.GetBuffer().size());
}

void LayerProfilerView::sendSelectedAddress(uint64_t address) {
  auto data = feedBackData(LayerInspectorMsgType::SelectedLayerAddress, address);
  if (m_WebSocketServer) m_WebSocketServer->SendData(data);
  if (m_TcpSocketClient) m_TcpSocketClient->sendData(data);
}

void LayerProfilerView::sendSerializeAttributeAddress(uint64_t address) {
  auto data = feedBackData(LayerInspectorMsgType::SerializeAttribute, address);
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
  if (!imageProvider->isImageExisted(imageID)) {
    imageProvider->setCurrentImageID(imageID);
    auto data = feedBackData(LayerInspectorMsgType::FlushImage, imageID);
    if (m_WebSocketServer) m_WebSocketServer->SendData(data);
    if (m_TcpSocketClient) m_TcpSocketClient->sendData(data);
  }
}

}  // namespace inspector