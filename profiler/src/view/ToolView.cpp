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

#include "ToolView.h"
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include "MainView.h"
#include "TracyFileRead.hpp"

ClientItem::ClientItem(ClientData& data, QWidget* parent) : QWidget(parent), data(data) {
  initWidget();
}

void ClientItem::initWidget() {
  auto layout = new QHBoxLayout(this);

  auto addressLable = new QLabel(this);
  addressLable->setText(data.address.c_str());
  auto procNameLable = new QLabel(this);
  procNameLable->setText(data.procName.c_str());

  auto divider = new QFrame(this);
  divider->setFrameShape(QFrame::VLine);
  divider->setFrameShadow(QFrame::Plain);
  divider->setLineWidth(1);

  layout->addWidget(addressLable);
  layout->addWidget(divider);
  layout->addWidget(procNameLable);
}

ToolView::ToolView(QWidget* parent) : QWidget(parent), port(8086), resolv(port) {
  startTimer(1);
  setAttribute(Qt::WA_StyledBackground);
  setStyleSheet("background-color: grey;");
  initView();
  initConnect();
}

void ToolView::reset() {
  clientWidget->clear();
  clients.clear();
  clientItems.clear();
  itemToClients.clear();
}

ToolView::~ToolView() {
  disconnect(clientWidget, &QListWidget::itemDoubleClicked, this, &ToolView::connectClient);
  clients.clear();
  for (auto iter : clientItems) {
    if (iter.second) {
      clientWidget->removeItemWidget(iter.second);
      delete iter.second;
    }
  }
  clientItems.clear();
  itemToClients.clear();
  resolvMap.clear();

  delete clientWidget;
  delete textCombobox;
  delete connectButton;
  delete openFileButton;
}

void ToolView::paintEvent(QPaintEvent* event) {
  resize(300, 300);
  return QWidget::paintEvent(event);
}

void ToolView::timerEvent(QTimerEvent* event) {
  updateBroadcastClients();
  update();
  QWidget::timerEvent(event);
}

void ToolView::initView() {
  auto layout = new QVBoxLayout(this);
  auto lable = new QLabel("TGFX Profiler v1.0.0", this);
  QFont font;
  font.setFamily("Arial");
  font.setPointSize(21);
  font.setBold(true);
  lable->setFont(font);
  lable->setStyleSheet("Color: white");
  lable->setAlignment(Qt::AlignCenter);

  textCombobox = new QComboBox;
  textCombobox->addItem("127.0.0.1");
  textCombobox->setEditable(true);

  auto buttonLayout = new QHBoxLayout;
  connectButton = new QPushButton("connect");
  openFileButton = new QPushButton("open file");
  auto websocketLayout = new QHBoxLayout;
  openWebsocketButton = new QPushButton("open websocket");

  buttonLayout->addWidget(connectButton);
  buttonLayout->addWidget(openFileButton);
  websocketLayout->addWidget(openWebsocketButton);

  clientWidget = new QListWidget;
  clientWidget->setResizeMode(QListView::Adjust);

  layout->addWidget(lable);
  layout->addWidget(textCombobox);
  layout->addLayout(buttonLayout);
  layout->addLayout(websocketLayout);
  layout->addWidget(clientWidget);
}

void ToolView::connectClient(QListWidgetItem* currenItem) {
  auto clientIdIter = itemToClients.find(currenItem);
  if (clientIdIter == itemToClients.end()) {
    return;
  }
  auto clientDataIter = clients.find(clientIdIter->second);
  if (clientDataIter == clients.end()) {
    return;
  }
  auto data = clientDataIter->second;
  auto mainView = static_cast<MainView*>(this->parent());
  mainView->connectClient(data.address.c_str(), data.port);
  reset();
}

void ToolView::connectAddress() {
  auto addr = textCombobox->currentText();
  auto byteArray = addr.toLatin1();
  auto aptr = byteArray.data();
  while (*aptr == ' ' || *aptr == '\t') aptr++;
  auto aend = aptr;
  while (*aend && *aend != ' ' && *aend != '\t') aend++;

  if (aptr != aend) {

    auto mainView = static_cast<MainView*>(this->parent());
    std::string address(aptr, aend);

    auto adata = address.data();
    auto ptr = adata + address.size() - 1;
    while (ptr > adata && *ptr != ':') ptr--;
    if (*ptr == ':') {
      std::string addrPart = std::string(adata, ptr);
      uint16_t portPart = (uint16_t)atoi(ptr + 1);
      mainView->connectClient(addrPart.c_str(), portPart);
    } else {
      mainView->connectClient(address.c_str(), 8086);
    }
  }
}

void ToolView::openFile() {
  auto mainView = static_cast<MainView*>(this->parent());
  mainView->openFile();
}

void ToolView::openWebsocketServer() {
  auto mainView = static_cast<MainView*>(this->parent());
  mainView->openWebsocketServer();
}

void ToolView::handleClient(uint64_t clientId) {
  auto iter = clientItems.find(clientId);
  if (iter != clientItems.end()) {
    return;
  }
  auto dataIter = clients.find(clientId);
  auto data = dataIter->second;

  std::string text;
  snprintf(text.data(), 1024, "%s(%s)", data.procName.c_str(), data.address.c_str());
  auto item = new QListWidgetItem(text.c_str(), clientWidget);
  item->setTextAlignment(Qt::AlignCenter);
  clientWidget->addItem(item);
  clientItems.emplace(clientId, item);
  itemToClients.emplace(item, clientId);
}

void ToolView::initConnect() {
  connect(connectButton, &QPushButton::clicked, this, &ToolView::connectAddress);
  connect(openFileButton, &QPushButton::clicked, this, &ToolView::openFile);
  connect(openWebsocketButton, &QPushButton::clicked, this, &ToolView::openWebsocketServer);
  connect(this, &ToolView::addClient, this, &ToolView::handleClient);
  connect(clientWidget, &QListWidget::itemClicked, this, &ToolView::connectClient);
}

void ToolView::updateBroadcastClients() {
  const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
  if (!broadcastListen) {
    broadcastListen = std::make_unique<tracy::UdpListen>();
    if (!broadcastListen->Listen(port)) {
      broadcastListen.reset();
    }
  } else {
    tracy::IpAddress addr;
    size_t len;
    for (;;) {
      auto msg = broadcastListen->Read(len, addr, 0);
      if (!msg) break;
      if (len > sizeof(tracy::BroadcastMessage)) continue;
      uint16_t broadcastVersion;
      memcpy(&broadcastVersion, msg, sizeof(uint16_t));
      if (broadcastVersion <= tracy::BroadcastVersion) {
        uint32_t protoVer;
        char procname[tracy::WelcomeMessageProgramNameSize];
        int32_t activeTime;
        uint16_t listenPort;
        uint64_t pid;

        switch (broadcastVersion) {
          case 3: {
            tracy::BroadcastMessage bm;
            memcpy(&bm, msg, len);
            protoVer = bm.protocolVersion;
            strcpy(procname, bm.programName);
            activeTime = bm.activeTime;
            listenPort = bm.listenPort;
            pid = bm.pid;
            break;
          }
          case 2: {
            if (len > sizeof(tracy::BroadcastMessage_v2)) continue;
            tracy::BroadcastMessage_v2 bm;
            memcpy(&bm, msg, len);
            protoVer = bm.protocolVersion;
            strcpy(procname, bm.programName);
            activeTime = bm.activeTime;
            listenPort = bm.listenPort;
            pid = 0;
            break;
          }
          case 1: {
            if (len > sizeof(tracy::BroadcastMessage_v1)) continue;
            tracy::BroadcastMessage_v1 bm;
            memcpy(&bm, msg, len);
            protoVer = bm.protocolVersion;
            strcpy(procname, bm.programName);
            activeTime = (int32_t)bm.activeTime;
            listenPort = (uint16_t)bm.listenPort;
            pid = 0;
            break;
          }
          case 0: {
            if (len > sizeof(tracy::BroadcastMessage_v0)) continue;
            tracy::BroadcastMessage_v0 bm;
            memcpy(&bm, msg, len);
            protoVer = bm.protocolVersion;
            strcpy(procname, bm.programName);
            activeTime = (int32_t)bm.activeTime;
            listenPort = 8086;
            pid = 0;
            break;
          }
          default: {
            activeTime = 0;
            listenPort = 0;
            pid = 0;
            assert(false);
            break;
          }
        }

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
                auto it = resolvMap.find(ip);
                assert(it != resolvMap.end());
                std::swap(it->second, name);
              });
            }
            resolvLock.unlock();
            clients.emplace(clientId, ClientData{time, protoVer, activeTime, listenPort, pid,
                                                 procname, std::move(ip)});
            Q_EMIT addClient(clientId);
          } else {
            it->second.time = time;
            it->second.activeTime = activeTime;
            it->second.port = listenPort;
            it->second.pid = pid;
            it->second.protocolVersion = protoVer;
            if (strcmp(it->second.procName.c_str(), procname) != 0) it->second.procName = procname;
          }
        } else if (it != clients.end()) {
          clients.erase(it);
        }
      }
    }
    auto it = clients.begin();
    while (it != clients.end()) {
      const auto diff = time - it->second.time;
      if (diff > 4000) {
        it = clients.erase(it);
      } else {
        ++it;
      }
    }
  }
}
