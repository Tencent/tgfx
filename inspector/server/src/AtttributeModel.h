#pragma once
#include <QAbstractItemModel>
#include <QQmlEngine>

#include "TaskTreeModel.h"


namespace isp {

class AtttributeModel : public QAbstractItemModel {
    Q_OBJECT
    Q_PROPERTY(bool isOpSelected READ getIsOpSelected WRITE setOpSelected NOTIFY opSelectedChanged)

public:
    explicit AtttributeModel(QObject* parent = nullptr);
    ~AtttributeModel() override;

    enum Roles {
        KeyRole = Qt::UserRole + 1,
        ValueRole
    };

    enum class AllDataTypes {
        Color,
        Vec4,
        Mat3,
        Int,
        Float,
        String
    };

    struct DataName {
        uint64_t name;
        AllDataTypes type;
        uint16_t size;
    };

    struct PropertyData {
        uint64_t name;
        uint64_t data;
        uint16_t size;
        enum class Type {Summary, Process} type;
    };


    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refreshAtttibuteData();
    bool isRunning();
    void setOpSelected(bool selected);
    bool getIsOpSelected() const { return isOpSelected; }


    Q_INVOKABLE void loadAttributeData(unsigned long long attributeDataPtr);
    Q_INVOKABLE void updateSelectedTask(const OperateData& opData, const QString& name);
    //Q_INVOKABLE void clearData();

    Q_SIGNAL void opSelectedChanged();



private:
    struct AtttributeItem {
        QString key;
        QVariant value;
    };

    QList<AtttributeItem> atttributeList;
    bool isOpSelected = false;

    std::vector<DataName> summaryName;
    std::vector<uint8_t> summaryData;
    std::vector<DataName> processesName;
    std::vector<uint8_t> processesData;

    std::unordered_map<uint64_t, PropertyData> properties;

    OperateData currentTaskData;
    QString currentTaskName;
    bool hasSelectedTask = false;

};

} // isp

