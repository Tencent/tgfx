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

#include "ToolView.h"
#include <MainView.h>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <TracyFileRead.hpp>
#include <cinttypes>

ClientItem::ClientItem(ClientData& data, QWidget* parent):
  data(data), QWidget(parent){
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

ToolView::ToolView(QWidget* parent) :
  port(8086),
  resolv(port),
  QWidget(parent) {
  startTimer(1);
  setAttribute(Qt::WA_StyledBackground);
  setStyleSheet("background-color: grey;");
  initView();
  initConnect();
}

ToolView::~ToolView() {

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
  // layout->setContentsMargins(0, 0, 0, 0);
  auto lable = new QLabel("TGFX Profiler v1.0.0", this);
  QFont font;
  font.setFamily("Arial");
  font.setPointSize(21);
  font.setBold(true);
  lable->setFont(font);
  lable->setStyleSheet("Color: white");
  lable->setAlignment(Qt::AlignCenter);

  textCombobox = new QComboBox;
  textCombobox->setEditable(true);

  auto buttonLayout = new QHBoxLayout;
  connectButton = new QPushButton("connect");
  openFileButton = new QPushButton("open file");

  buttonLayout->addWidget(connectButton);
  buttonLayout->addWidget(openFileButton);

  clientWidget = new QListWidget;
  clientWidget->setResizeMode(QListView::Adjust);

  layout->addWidget(lable);
  layout->addWidget(textCombobox);
  layout->addLayout(buttonLayout);
  layout->addWidget(clientWidget);
  // layout->addWidget(itemWidget);
}

void ToolView::connectClient(QListWidgetItem* currenItem, QListWidgetItem*) {
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
}

void ToolView::connect() {
  auto mainView = static_cast<MainView*>(this->parent());
  mainView->openConnectView();
}

void ToolView::openFile() {
  auto mainView = static_cast<MainView*>(this->parent());
  mainView->openFile();
}

void ToolView::handleClient(uint64_t clientId) {
  auto iter = clientItems.find(clientId);
  if (iter != clientItems.end()) {
    return;
  }
  auto dataIter = clients.find(clientId);
  auto data = dataIter->second;

  std::string text;
  sprintf(text.data(), "%s(%s)", data.procName.c_str(), data.address.c_str());
  auto item = new QListWidgetItem(text.c_str(), clientWidget);
  item->setTextAlignment(Qt::AlignCenter);
  clientWidget->addItem(item);
  clientItems.emplace(clientId, item);
  itemToClients.emplace(item, clientId);
}

void ToolView::initConnect() {
  QObject::connect(connectButton, &QPushButton::clicked, this, &ToolView::connect);
  QObject::connect(openFileButton, &QPushButton::clicked, this, &ToolView::openFile);
  QObject::connect(this, &ToolView::addClient, this, &ToolView::handleClient);
  QObject::connect(clientWidget, &QListWidget::currentItemChanged, this, &ToolView::connectClient);
}

void ToolView::updateBroadcastClients()
{
    const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if(!broadcastListen)
    {
        broadcastListen = std::make_unique<tracy::UdpListen>();
        if(!broadcastListen->Listen(port)) {
            broadcastListen.reset();
        }
    }
    else
    {
        tracy::IpAddress addr;
        size_t len;
        for(;;) {
          auto msg = broadcastListen->Read(len, addr, 0);
          if(!msg) break;
          if(len > sizeof(tracy::BroadcastMessage)) continue;
          uint16_t broadcastVersion;
          memcpy(&broadcastVersion, msg, sizeof(uint16_t));
          if(broadcastVersion <= tracy::BroadcastVersion) {
              uint32_t protoVer;
              char procname[tracy::WelcomeMessageProgramNameSize];
              int32_t activeTime;
              uint16_t listenPort;
              uint64_t pid;

              switch(broadcastVersion) {
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
                  if(len > sizeof(tracy::BroadcastMessage_v2)) continue;
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
                  if(len > sizeof(tracy::BroadcastMessage_v1)) continue;
                  tracy::BroadcastMessage_v1 bm;
                  memcpy(&bm, msg, len);
                  protoVer = bm.protocolVersion;
                  strcpy(procname, bm.programName);
                  activeTime = bm.activeTime;
                  listenPort = bm.listenPort;
                  pid = 0;
                  break;
              }
              case 0: {
                  if(len > sizeof(tracy::BroadcastMessage_v0)) continue;
                  tracy::BroadcastMessage_v0 bm;
                  memcpy(&bm, msg, len);
                  protoVer = bm.protocolVersion;
                  strcpy(procname, bm.programName);
                  activeTime = bm.activeTime;
                  listenPort = 8086;
                  pid = 0;
                  break;
              }
              default:
                  assert(false);
                  break;
              }

              auto address = addr.GetText();
              const auto ipNumerical = addr.GetNumber();
              const auto clientId = uint64_t(ipNumerical) | (uint64_t(listenPort) << 32);
              auto it = clients.find(clientId);
              if(activeTime >= 0) {
                  if(it == clients.end())
                  {
                      std::string ip(address);
                      resolvLock.lock();
                      if(resolvMap.find(ip) == resolvMap.end())
                      {
                          resolvMap.emplace(ip, ip);
                          resolv.Query(ipNumerical, [&, ip] (std::string&& name) {
                              std::lock_guard<std::mutex> lock(resolvLock);
                              auto it = resolvMap.find(ip);
                              assert(it != resolvMap.end());
                              std::swap(it->second, name);
                              });
                      }
                      resolvLock.unlock();
                      clients.emplace(clientId, ClientData { time, protoVer, activeTime, listenPort, pid, procname, std::move(ip) });
                      Q_EMIT addClient(clientId);
                  }
                  else
                  {
                      it->second.time = time;
                      it->second.activeTime = activeTime;
                      it->second.port = listenPort;
                      it->second.pid = pid;
                      it->second.protocolVersion = protoVer;
                      if(strcmp(it->second.procName.c_str(), procname) != 0) it->second.procName = procname;
                  }
              }
              else if(it != clients.end()) {
                  clients.erase(it);
              }
          }
        }
        auto it = clients.begin();
        while(it != clients.end()) {
            const auto diff = time - it->second.time;
            if(diff > 4000)  {
                it = clients.erase(it);
            }
            else {
                ++it;
            }
        }
    }
}
