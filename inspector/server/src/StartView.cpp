#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include "StartView.h"
#include <QAbstractListModel>
#include "StatisticModel.h"
#include "DataChartItem.h"
#include "View.h"
#include <QQmlContext>

namespace isp {
    ClientItem::ClientItem(const ClientData &data, QObject *parent) : QObject(parent), data(data) {
        uint32_t ipNumerical = 0;
        std::string ipStr = data.caddress;
        uint32_t components[4] = {0};
        int componentIdx = 0;
        uint32_t value = 0;

        for (auto c : ipStr) {
            if (c == '.') {
                if (componentIdx < 4) {
                    components[componentIdx++] = value;
                }
                value = 0;
            } else if (c >= '0' && c <= '9') {
                value = value * 10 + static_cast<uint32_t>(c - '0');
            } else {
                break;
            }
        }
        if (componentIdx < 4) {
            components[componentIdx++] = value;
        }
        if (componentIdx == 4) {
            ipNumerical = (components[0] << 24) | (components[1] << 16) | (components[2] << 8) | components[3];
        }
        clientId = uint64_t(ipNumerical) | (uint64_t(data.ports) << 32);
    }

    StartView::StartView(QObject *parent) :QObject(parent), resolv(port) {
        loadRecentFiles();

        broadcastTimer = new QTimer(this);
        connect(broadcastTimer, &QTimer::timeout, this, &StartView::updateBroadcastClients);
        connect(this, &StartView::addClient, this, &StartView::handleClient);
        broadcastTimer->start(1000);
    }

    StartView::~StartView() {
        saveRecentFiles();
        qDeleteAll(fileItems);

        if(broadcastTimer) {
            broadcastTimer->stop();
            delete broadcastTimer;
        }

        for(auto&& item : clientItems) {
            delete item.second;
        }
        clientItems.clear();
        clients.clear();
        broadcastListen.reset();

    }

    QList<QObject *> StartView::getFileItems() const {
        QList<QObject*> items;
        for (const auto &fileItem : fileItems) {
            items.append(fileItem);
        }

        return items;
    }

    void StartView::openFile(const QString &fPath) {
        if(fPath.isEmpty() || !QFileInfo::exists(fPath)) {
            return;
        }

        lastOpenFile = fPath;
        addRecentFile(fPath);
        Q_EMIT lastOpenFileChanged();

        if(filesPath) {
            filesPath->setText(fPath);
        }

        auto file = std::shared_ptr<tracy::FileRead>(tracy::FileRead::Open(fPath.toStdString().c_str()));
        if (file) {
           inspectorView = new InspectorView(*file, 1920, Config(), this);
            inspectorView->initView();
            Q_EMIT closeWindow();
        }
    }

    void StartView::addRecentFile(const QString &fPath) {
        if(fPath.isEmpty()) {

            return;
        }

        recentFiles.removeAll(fPath);
        recentFiles.prepend(fPath);

        if(lastOpenFile != fPath) {
            lastOpenFile = fPath;
            Q_EMIT lastOpenFileChanged();
        }

        while (recentFiles.size() >= 15) {
            recentFiles.removeLast();
        }

        qDeleteAll(fileItems);
        fileItems.clear();

        for(const QString &file : recentFiles) {
            fileItems.append(new FileItem(file, QFileInfo(file).fileName(), this));
        }

        Q_EMIT fileItemsChanged();
        Q_EMIT recentFilesChanged();

        saveRecentFiles();

    }

    void StartView::clearRecentFiles() {
        recentFiles.clear();
        qDeleteAll(fileItems);
        fileItems.clear();

        Q_EMIT recentFilesChanged();
        Q_EMIT fileItemsChanged();

        saveRecentFiles();

    }

    QList<QObject *> StartView::getClientItems() const {
        QList<QObject*> items;
        for(const auto& pair : clientItems) {
            items.append(pair.second);
        }
        return items;
    }

    void StartView::connectToClient(uint64_t clientId) {
        auto it = clients.find(clientId);
        if(it != clients.end()) {
            Q_EMIT openConnectView(QString::fromStdString(it->second.caddress), it->second.ports);
        }
    }

    void StartView::connectToAddress(const QString &address) {
        std::string addr = address.toStdString();
        auto aptr = addr.c_str();
        while (*aptr == ' ' || *aptr == '\t') aptr++;
        auto aend = aptr;
        while (*aend && *aend != ' ' && *aend != '\t') aend++;

        if(aptr != aend) {
            std::string addressStr(aptr, aend);
            auto adata = addressStr.data();
            auto ptr = adata + addressStr.size() - 1;
            while (ptr > adata && *ptr != ':') ptr--;

            if(*ptr == ':') {
                std::string addrPart = std::string(adata, ptr);
                uint16_t portPart = (uint16_t)atoi(ptr + 1);
                Q_EMIT openConnectView(QString::fromStdString(addrPart), portPart);
            }else {
                Q_EMIT openConnectView(QString::fromStdString(addressStr), 8086);
            }
        }
    }


    void StartView::showStartView() {
        QQmlApplicationEngine* engine = new QQmlApplicationEngine(this);
        engine->rootContext()->setContextProperty("startViewModel", this);
        engine->load(QUrl(QStringLiteral("qrc:/qml/StartView.qml")));

        if (!engine->rootObjects().isEmpty()) {
            auto startWindow = static_cast<QQuickWindow*>(engine->rootObjects().first());
            startWindow->setFlags(Qt::Window);
            startWindow->setTitle("Inspector - Start");
            startWindow->resize(1000, 600);
            startWindow->show();
        } else {
            qWarning() << "无法加载StartView.qml";
        }

    }

    void StartView::loadRecentFiles() {
        QSettings settings("MyCompany", "Inspector");
        recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();

        QStringList validFiles;
        for(const QString &file : recentFiles) {
            if(QFileInfo::exists(file)) {
                validFiles.append(file);
            }
        }

        recentFiles = validFiles;
        qDeleteAll(fileItems);
        fileItems.clear();

        for(const QString &file : recentFiles) {
            fileItems.append(new FileItem(file, QFileInfo(file).fileName(), this));
        }

        Q_EMIT recentFilesChanged();
        Q_EMIT fileItemsChanged();
    }

    void StartView::saveRecentFiles() {
        QSettings settings("MyCompany", "Inspector");
        settings.setValue(QStringLiteral("recentFiles"), recentFiles);
        settings.sync();
    }

    //todo: need new socket to handle the connect
    void StartView::updateBroadcastClients() {
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
            it->second.ports = listenPort;
            it->second.pid = pid;
            it->second.protocolVersion = protoVer;
            if (strcmp(it->second.procName.c_str(), procname) != 0) it->second.procName = procname;
          }
        } else if (it != clients.end()) {
          clients.erase(it);
            auto itemIt = clientItems.find(clientId);
            if (itemIt != clientItems.end()) {
                delete itemIt->second;
                clientItems.erase(itemIt);
                Q_EMIT clientItemsChanged();
            }
        }
      }
    }
    auto it = clients.begin();
    while (it != clients.end()) {
      const auto diff = time - it->second.time;
      if (diff > 4000) {
          auto itemIt = clientItems.find(it->first);
          if (itemIt != clientItems.end()) {
              delete itemIt->second;
              clientItems.erase(itemIt);
          }
        it = clients.erase(it);
          Q_EMIT clientItemsChanged();
      } else {
        ++it;
      }
    }
  }
    }

    void StartView::handleClient(uint64_t clientId) {
        auto iter = clientItems.find(clientId);
        if(iter != clientItems.end()) {
            delete iter->second;
            clientItems.erase(iter);
        }

        auto dataIter = clients.find(clientId);
        if(dataIter != clients.end()) {
            auto item = new ClientItem(dataIter->second, this);
            clientItems[clientId] = item;
            Q_EMIT clientItemsChanged();
        }
    }
}
