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
#include <QComboBox>
#include <QPushButton>
#include <QListWidgetItem>
#include <QWidget>
#include "TracySocket.hpp"
#include "tracy_robin_hood.h"
#include "profiler/src/ResolvService.hpp"

struct ClientData
{
  int64_t time;
  uint32_t protocolVersion;
  int32_t activeTime;
  uint16_t port;
  uint64_t pid;
  std::string procName;
  std::string address;
};

class ClientItem: public QWidget {
public:
  ClientItem(ClientData& data, QWidget* parent);
  void initWidget();
private:
  ClientData& data;
};

class ToolView: public QWidget {
  Q_OBJECT
public:
  ToolView(QWidget* parent);
  ~ToolView();
  void paintEvent(QPaintEvent *event) override;
  void timerEvent(QTimerEvent* event) override;
  void updateBroadcastClients();

  void initView();
  void initConnect();
  void openFile();
  void reset();
  Q_SLOT void connectAddress();
  Q_SLOT void connectClient(QListWidgetItem* currenItem);
  Q_SLOT void handleClient(uint64_t clinetId);
  Q_SIGNAL void addClient(uint64_t clinetId);
private:
  QComboBox* textCombobox;
  QPushButton* connectButton;
  QPushButton* openFileButton;

  QListWidget* clientWidget;

  std::mutex resolvLock;
  uint16_t port;
  ResolvService resolv;
  std::unique_ptr<tracy::UdpListen> broadcastListen;
  tracy::unordered_flat_map<uint64_t, ClientData> clients;
  tracy::unordered_flat_map<uint64_t, QListWidgetItem*> clientItems;
  tracy::unordered_flat_map<QListWidgetItem*, uint64_t> itemToClients;
  tracy::unordered_flat_map<std::string, std::string> resolvMap;
};
