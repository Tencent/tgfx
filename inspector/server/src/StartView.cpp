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

#include "StartView.h"
#include <QFileInfo>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include "FramesDrawer.h"
#include "Protocol.h"
#include "Socket.h"

namespace inspector {
ClientData::ClientData(int64_t time, uint32_t protoVer, int32_t activeTime, uint16_t port,
                       uint64_t pid, std::string procName, std::string address, uint8_t type)
    : time(time), protocolVersion(protoVer), activeTime(activeTime), port(port), pid(pid),
      procName(std::move(procName)), address(std::move(address)), type(type) {
}

StartView::StartView(QObject* parent) : QObject(parent), resolv(port) {
  loadRecentFiles();

  broadcastTimer = new QTimer(this);
  connect(broadcastTimer, &QTimer::timeout, this, &StartView::updateBroadcastClients);
  broadcastTimer->start(1000);
}

StartView::~StartView() {
  saveRecentFiles();
  qDeleteAll(fileItems);
  if (broadcastTimer) {
    broadcastTimer->stop();
    delete broadcastTimer;
  }
  for (auto& it : clients) {
    delete it.second;
  }
  clients.clear();
  broadcastListen.reset();
  Q_EMIT quitStartView();
}

QList<QObject*> StartView::getFileItems() const {
  QList<QObject*> items;
  for (const auto& fileItem : fileItems) {
    items.append(fileItem);
  }
  return items;
}

void StartView::openFile(const QString& fPath) {
  if (fPath.isEmpty() || !QFileInfo::exists(fPath)) {
    return;
  }

  addRecentFile(fPath);
  inspectorView = new InspectorView(fPath.toStdString(), 1920, this);
}

void StartView::openFile(const QUrl& filePath) {
  openFile(filePath.path());
}

void StartView::addRecentFile(const QString& fPath) {
  if (fPath.isEmpty()) {
    return;
  }
  recentFiles.removeAll(fPath);
  recentFiles.prepend(fPath);
  if (lastOpenFile != fPath) {
    lastOpenFile = fPath;
    Q_EMIT lastOpenFileChanged();
  }
  while (recentFiles.size() >= 15) {
    recentFiles.removeLast();
  }
  qDeleteAll(fileItems);
  fileItems.clear();
  for (const QString& file : recentFiles) {
    fileItems.append(new FileItem(file, QFileInfo(file).fileName(), this));
  }
  Q_EMIT fileItemsChanged();
  saveRecentFiles();
}

void StartView::clearRecentFiles() {
  recentFiles.clear();
  qDeleteAll(fileItems);
  fileItems.clear();

  Q_EMIT fileItemsChanged();

  saveRecentFiles();
}

QVector<QObject*> StartView::getFrameCaptureClientItems() const {
  QVector<QObject*> clientDatas;
  for (auto& client : clients) {
    if (client.second->type == FrameCapture) {
      clientDatas.push_back(client.second);
    }
  }
  return clientDatas;
}

QVector<QObject*> StartView::getLayerTreeClientItems() const {
  QVector<QObject*> clientDatas;
  for (auto& client : clients) {
    if (client.second->type == LayerTree) {
      clientDatas.push_back(client.second);
    }
  }
  return clientDatas;
}

void StartView::connectToClient(QObject* object) {
  auto client = dynamic_cast<ClientData*>(object);
  if (client) {
    if (inspectorView) {
      connect(inspectorView, &InspectorView::destroyed, this, [&, client]() {
        inspectorView = new InspectorView(client, 1920, this);
        connect(inspectorView, &InspectorView::viewHide, [this]() { showStartView(); });
      });
      inspectorView->deleteLater();
    } else {
      inspectorView = new InspectorView(client, 1920, this);
      connect(inspectorView, &InspectorView::viewHide, [this]() { showStartView(); });
    }
  }
}

void StartView::connectToClientByLayerInspector(QObject* object) {
  auto client = dynamic_cast<ClientData*>(object);
  if (client) {
    if (layerProfilerView) {
      connect(layerProfilerView, &LayerProfilerView::destroyed, this, [&, client] {
        layerProfilerView =
            new LayerProfilerView(QString::fromStdString(client->address), client->port);
        connect(layerProfilerView, &LayerProfilerView::viewHide, [this]() { showStartView(); });
      });
      layerProfilerView->deleteLater();
    } else {
      layerProfilerView =
          new LayerProfilerView(QString::fromStdString(client->address), client->port);
      connect(layerProfilerView, &LayerProfilerView::viewHide, [this]() { showStartView(); });
    }
  }
}

void StartView::showStartView() {
  if (!qmlEngine) {
    qmlEngine = new QQmlApplicationEngine(this);
    qmlEngine->rootContext()->setContextProperty("startViewModel", this);
    qmlEngine->load(QUrl(QStringLiteral("qrc:/qml/StartView.qml")));

    if (!qmlEngine->rootObjects().isEmpty()) {
      auto startWindow = dynamic_cast<QQuickWindow*>(qmlEngine->rootObjects().first());
      startWindow->setFlags(Qt::Window);
      startWindow->setTitle("Inspector - Start");
      startWindow->resize(1000, 600);
      startWindow->show();

      connect(startWindow, &QQuickWindow::closing, this, &StartView::onCloseAllView);
      connect(this, &StartView::quitStartView, QApplication::instance(), &QApplication::quit);
    } else {
      qWarning() << "无法加载StartView.qml";
    }
  }

  if (!qmlEngine->rootObjects().isEmpty()) {
    auto startWindow = dynamic_cast<QQuickWindow*>(qmlEngine->rootObjects().first());
    startWindow->show();
  }
}

void StartView::loadRecentFiles() {
  QSettings settings("MyCompany", "Inspector");
  recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();

  QStringList validFiles;
  for (const QString& file : recentFiles) {
    if (QFileInfo::exists(file)) {
      validFiles.append(file);
    }
  }

  recentFiles = validFiles;
  qDeleteAll(fileItems);
  fileItems.clear();

  for (const QString& file : recentFiles) {
    fileItems.append(new FileItem(file, QFileInfo(file).fileName(), this));
  }

  Q_EMIT fileItemsChanged();
}

void StartView::saveRecentFiles() {
  QSettings settings("MyCompany", "Inspector");
  settings.setValue(QStringLiteral("recentFiles"), recentFiles);
  settings.sync();
}

void StartView::updateBroadcastClients() {
  const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
  if (!broadcastListen) {
    bool isListen = false;
    broadcastListen = std::make_unique<UdpListen>();
    for (uint16_t i = 0; i < broadcastNum; i++) {
      isListen = broadcastListen->Listen(port + i);
      if (isListen) {
        break;
      }
    }
    if (!isListen) {
      broadcastListen.reset();
    }
  } else {
    IpAddress addr;
    size_t len;
    for (;;) {
      auto msg = broadcastListen->Read(len, addr, 0);
      if (!msg) {
        break;
      }
      if (len > sizeof(BroadcastMessage)) {
        continue;
      }
      BroadcastMessage bm = {};
      memcpy(&bm, msg, len);
      auto protoVer = bm.protocolVersion;
      char procname[WelcomeMessageProgramNameSize];
      strcpy(procname, bm.programName);
      auto activeTime = bm.activeTime;
      auto listenPort = bm.listenPort;
      auto pid = bm.pid;
      auto type = bm.type;

      auto address = addr.GetText();
      const auto ipNumerical = addr.GetNumber();
      const auto clientId = uint64_t(ipNumerical) | (uint64_t(listenPort) << 32);
      auto it = clients.find(clientId);
      if (activeTime >= 0) {
        if (it == clients.end()) {
          std::string ip(address);

          resolvLock.lock();
          if (resolvMap.find(ip) == resolvMap.end()) {
            resolvMap.emplace(ip, ip);
            resolv.Query(ipNumerical, [&, ip](std::string&& name) {
              std::lock_guard<std::mutex> lock(resolvLock);
              auto iter = resolvMap.find(ip);
              assert(iter != resolvMap.end());
              std::swap(iter->second, name);
            });
          }
          resolvLock.unlock();
          auto client = new ClientData{time, protoVer, activeTime,    listenPort,
                                       pid,  procname, std::move(ip), type};
          clients.emplace(clientId, client);
          Q_EMIT clientItemsChanged();
        } else {
          auto client = it->second;
          client->time = time;
          client->activeTime = activeTime;
          client->port = listenPort;
          client->pid = pid;
          client->protocolVersion = protoVer;
          if (strcmp(client->procName.c_str(), procname) != 0) {
            client->procName = procname;
          }
          client->type = type;
        }
      } else if (it != clients.end()) {
        clients.erase(it);
        Q_EMIT clientItemsChanged();
      }
    }
    auto it = clients.begin();
    while (it != clients.end()) {
      const auto diff = time - it->second->time;
      if (diff > 4000) {
        it = clients.erase(it);
        Q_EMIT clientItemsChanged();
      } else {
        ++it;
      }
    }
  }
}

void StartView::onCloseView(QObject* view) {
  view->deleteLater();
  if (view == inspectorView) {
    inspectorView = nullptr;
  }
  if (view == layerProfilerView) {
    layerProfilerView = nullptr;
  }
}

void StartView::onCloseAllView() {
  if (inspectorView) {
    inspectorView->deleteLater();
  }
  if (layerProfilerView) {
    layerProfilerView->deleteLater();
  }
  this->deleteLater();
}
}  // namespace inspector
