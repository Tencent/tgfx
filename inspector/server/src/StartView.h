#pragma once


#include <QObject>
#include <QLabel>
#include <QStringList>
#include <QDateTime>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QtQml/qqmlengine.h>
#include <QtTest/QTest>
#include <QTimer>
#include <memory>
#include "TracySocket.hpp"
#include "src/ResolvService.hpp"
#include "tracy_robin_hood.h"
#include "InspectorView.h"

namespace isp {

    struct ClientData {
        int64_t time;
        uint32_t protocolVersion;
        int32_t activeTime;
        uint16_t ports;
        uint64_t pid;
        std::string procName;
        std::string caddress;
    };

    class InspectorView;
    class ClientItem : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString procName READ getProcName CONSTANT)
        Q_PROPERTY(QString caddress READ getAddress CONSTANT)
        Q_PROPERTY(uint64_t clientId READ getClientId CONSTANT)
        Q_PROPERTY(uint16_t ports READ getPort CONSTANT)
        Q_PROPERTY(QString clientName READ getClientName CONSTANT)
        Q_PROPERTY(QString address READ getAddressWithPort CONSTANT)
        Q_PROPERTY(uint16_t port READ getPort CONSTANT)


    public:
        explicit ClientItem(const ClientData& data, QObject* parent = nullptr);
        QString getProcName() const { return QString::fromStdString(data.procName); }
        QString getAddress() const { return QString::fromStdString(data.caddress); }
        QString getAddressWithPort() const { return QString::fromStdString(data.caddress); }
        QString getClientName() const { return QString::fromStdString(data.procName); }
        uint64_t getClientId() const { return clientId;}
        uint16_t getPort() const { return data.ports; }

    private:
        ClientData data;
        uint64_t clientId;
    };

    class FileItem : public QObject {
        Q_OBJECT
        Q_PROPERTY(QString filesPath READ getFilePath CONSTANT)
        Q_PROPERTY(QString filesName READ getFileName CONSTANT)
        Q_PROPERTY(QDateTime lastOpened READ getLastOpened CONSTANT)


    public:
        explicit FileItem(const QString &fsPath, const QString &fsName, QObject *parent) :QObject(parent), filesPath(fsPath), filesName(fsName), lastOpened(QDateTime::currentDateTime()) {
        }

        QString getFilePath() const { return filesPath; }
        QString getFileName() const { return QFileInfo(filesPath).fileName(); }
        QDateTime getLastOpened() const { return lastOpened; }

    private:
        QString filesPath;
        QString filesName;
        QDateTime lastOpened;
    };

    class StartView : public QObject {
        Q_OBJECT
        Q_PROPERTY(QList<QObject*> fileItems READ getFileItems NOTIFY fileItemsChanged)
        Q_PROPERTY(QList<QObject*> clientItems READ getClientItems NOTIFY clientItemsChanged)
        Q_PROPERTY(QString lastOpenFile READ getLastOpenFile NOTIFY lastOpenFileChanged)

    public:
        explicit StartView(QObject *parent = nullptr);
        ~StartView() override;

        void setFilePathLabel(QLabel* label) { filesPath = label; }
        QStringList getRecentFiles() const { return recentFiles; }
        QString getLastOpenFile() const { return lastOpenFile; }

        void setQmlEngine(QQmlApplicationEngine* engine) { qmlEngine = engine; }



        ///* file items *///
        Q_INVOKABLE QList<QObject*> getFileItems() const;
        Q_INVOKABLE void openFile(const QString &fPath);
        Q_INVOKABLE void addRecentFile(const QString &fPath);
        Q_INVOKABLE void clearRecentFiles();
        Q_INVOKABLE QString getFileNameFromPath(const QString &fPath){ return QFileInfo(fPath).fileName(); }
        Q_INVOKABLE QString getDirectoryFromPath(const QString &fPath){ return QFileInfo(fPath).path(); }

        Q_SIGNAL void recentFilesChanged();
        Q_SIGNAL void fileItemsChanged();
        Q_SIGNAL void lastOpenFileChanged();
        Q_SIGNAL void openStatView(const QString &fPath);

        ///* client items *///
        Q_INVOKABLE QList<QObject*> getClientItems() const;
        Q_INVOKABLE void connectToClient(uint64_t clientId);
        Q_INVOKABLE void connectToAddress(const QString &address);

        Q_SIGNAL void clientItemsChanged();
        Q_SIGNAL void addClient(uint64_t clientId);
        Q_SIGNAL void openConnectView(const QString &address, uint16_t port);
        Q_SIGNAL void closeWindow();

        Q_INVOKABLE void showStartView();

    protected:
        void loadRecentFiles();
        void saveRecentFiles();

        void updateBroadcastClients();
        void handleClient(uint64_t clientId);

    private:
        InspectorView* inspectorView = nullptr;
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
        std::unique_ptr<tracy::UdpListen> broadcastListen;
        tracy::unordered_flat_map<uint64_t, ClientData> clients;
        tracy::unordered_flat_map<uint64_t, QObject*> clientItems;
        tracy::unordered_flat_map<QObject*, uint64_t> itemToClients;
        tracy::unordered_flat_map<std::string, std::string> resolvMap;
        QTimer* broadcastTimer = nullptr;
    };
}


