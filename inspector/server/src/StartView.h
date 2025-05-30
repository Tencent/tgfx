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

#include <QtQml/qqmlengine.h>
#include <qstring.h>
#include <QDateTime>
#include <QLabel>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QtTest/QTest>
#include "InspectorView.h"
#include "ResolvService.h"
#include "Socket.h"
// #include "layerInspector/LayerProfilerView.h"

namespace inspector {

class ClientData : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString procName READ getProcName CONSTANT)
  Q_PROPERTY(QString address READ getAddress CONSTANT)
  Q_PROPERTY(uint16_t port READ getPort CONSTANT)
 public:
  ClientData(int64_t time, uint32_t protoVer, int32_t activeTime, uint16_t port, uint64_t pid,
             std::string procName, std::string address);

  QString getProcName() const {
    return QString::fromStdString(procName);
  }
  QString getAddress() const {
    return QString::fromStdString(address);
  }
  uint16_t getPort() const {
    return port;
  }

  int64_t time = 0;
  uint32_t protocolVersion = 0;
  int32_t activeTime = 0;
  uint16_t port = 0;
  uint64_t pid = 0;
  std::string procName;
  std::string address;
};

class InspectorView;
class FileItem : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString filesPath READ getFilePath CONSTANT)
  Q_PROPERTY(QString filesName READ getFileName CONSTANT)
  Q_PROPERTY(QDateTime lastOpened READ getLastOpened CONSTANT)

 public:
  explicit FileItem(const QString& fsPath, const QString& fsName, QObject* parent)
      : QObject(parent), filesPath(fsPath), filesName(fsName),
        lastOpened(QDateTime::currentDateTime()) {
  }

  QString getFilePath() const {
    return filesPath;
  }
  QString getFileName() const {
    return QFileInfo(filesPath).fileName();
  }
  QDateTime getLastOpened() const {
    return lastOpened;
  }

 private:
  QString filesPath;
  QString filesName;
  QDateTime lastOpened;
};

class StartView : public QObject {
  Q_OBJECT
  Q_PROPERTY(QList<QObject*> fileItems READ getFileItems NOTIFY fileItemsChanged)
  Q_PROPERTY(QVector<QObject*> clientItems READ getClientItems NOTIFY clientItemsChanged)
  Q_PROPERTY(QString lastOpenFile READ getLastOpenFile NOTIFY lastOpenFileChanged)

 public:
  explicit StartView(QObject* parent = nullptr);
  ~StartView() override;

  void setFilePathLabel(QLabel* label) {
    filesPath = label;
  }
  QStringList getRecentFiles() const {
    return recentFiles;
  }
  QString getLastOpenFile() const {
    return lastOpenFile;
  }

  void setQmlEngine(QQmlApplicationEngine* engine) {
    qmlEngine = engine;
  }

  ///* file items *///
  Q_INVOKABLE QList<QObject*> getFileItems() const;
  Q_INVOKABLE void openFile(const QString& fPath);
  Q_INVOKABLE void addRecentFile(const QString& fPath);
  Q_INVOKABLE void clearRecentFiles();
  Q_INVOKABLE QString getFileNameFromPath(const QString& fPath) {
    return QFileInfo(fPath).fileName();
  }
  Q_INVOKABLE QString getDirectoryFromPath(const QString& fPath) {
    return QFileInfo(fPath).path();
  }

  Q_SIGNAL void recentFilesChanged();
  Q_SIGNAL void fileItemsChanged();
  Q_SIGNAL void lastOpenFileChanged();
  Q_SIGNAL void openStatView(const QString& fPath);

  ///* client items *///
  Q_INVOKABLE QVector<QObject*> getClientItems() const;

  Q_INVOKABLE void connectToClient(QObject* object);
  Q_INVOKABLE void connectToClientByLayerInspector(QObject* object);

  Q_SIGNAL void clientItemsChanged();
  Q_SIGNAL void openConnectView(const QString& address, uint16_t port);
  Q_SIGNAL void closeWindow();

  Q_INVOKABLE void showStartView();

 protected:
  void loadRecentFiles();
  void saveRecentFiles();

  void updateBroadcastClients();

 private:
  InspectorView* inspectorView = nullptr;
  // LayerProfilerView* layerProfilerView = nullptr;

  QLabel* filesPath = nullptr;
  QString lastOpenFile;
  QStringList recentFiles;
  QList<FileItem*> fileItems;
  QQmlApplicationEngine* qmlEngine = nullptr;

  //todo: need to refactor
  //only for test
  std::mutex resolvLock;
  uint16_t port = 8086;
  ResolvService resolv;
  std::unique_ptr<UdpListen> broadcastListen;
  std::unordered_map<uint64_t, ClientData*> clients;
  std::unordered_map<std::string, std::string> resolvMap;
  QTimer* broadcastTimer = nullptr;
};
}  // namespace inspector
