#include "AtttributeModel.h"
#include <QAbstractItemModel>

namespace isp {
    AtttributeModel::AtttributeModel(QObject *parent) : QAbstractItemModel(parent) {
        refreshAtttibuteData();  // 初始化时加载测试数据
    }

    AtttributeModel::~AtttributeModel() {
    }

    QModelIndex AtttributeModel::index(int row, int column, const QModelIndex &parent) const {
        if(!parent.isValid() && row >= 0 && row < atttributeList.size()) {
            return createIndex(row, column);
        }

        return QModelIndex();
    }

    QModelIndex AtttributeModel::parent(const QModelIndex &child) const {
        Q_UNUSED(child);
        return QModelIndex();
    }

    int AtttributeModel::rowCount(const QModelIndex &parent) const {
        if(parent.isValid()) {
            return 0;
        }

        return static_cast<int>(atttributeList.size());
    }

    int AtttributeModel::columnCount(const QModelIndex &parent) const {
        Q_UNUSED(parent);
        return 2;
    }

    QVariant AtttributeModel::data(const QModelIndex &index, int role) const {
        if(!index.isValid() || index.row() < 0 || index.row() >= atttributeList.size() ) {
            return QVariant();
        }

        const AtttributeItem& item = atttributeList.at(index.row());

        if(role ==  KeyRole) {
            return item.key;
        }
        else if(role == ValueRole) {
            return item.value;
        }

        return QVariant();
    }

    QHash<int, QByteArray> AtttributeModel::roleNames() const {
        QHash<int, QByteArray> roles;
        roles[KeyRole] = "key";
        roles[ValueRole] = "value";
        return roles;
    }

    void AtttributeModel::refreshAtttibuteData() {
        if(hasSelectedTask) {
            updateSelectedTask(currentTaskData, currentTaskName);
            return;
        }
        beginResetModel();
        atttributeList.clear();

        // 生成summary数据
        QList<QPair<QString, QVariant>> summaryData = {
            {"Primitive", "TriangleStrip"},
            {"rectCount", "1"},
            {"commonColor", "(1,1,1,1)"},
            {"uvMatrix", "nullptr"},
            {"aaType", "None"},
            {"scissorRect", "(0,0,0,0)"},
            {"blendMode", "SrcOver"}
        };

        // 生成processes数据
        QList<QPair<QString, QVariant>> processesData = {
            {"colors", "nullptr"},
            {"Coverage", "nullptr"}
        };

        // 添加summary数据标题
        atttributeList.append(AtttributeItem{"Summary", ""});
        for(const auto& item : summaryData) {
            atttributeList.append(AtttributeItem{item.first, item.second});
        }

        // 添加processes数据标题
        atttributeList.append(AtttributeItem{"Processes", ""});
        for(const auto& item : processesData) {
            atttributeList.append(AtttributeItem{item.first, item.second});
        }

        endResetModel();
    }

    bool AtttributeModel::isRunning() {
        //todo: connect to worker
        return false;
    }

    void AtttributeModel::setOpSelected(bool selected) {
        if(isOpSelected != selected) {
            isOpSelected = selected;
            Q_EMIT opSelectedChanged();
            refreshAtttibuteData();
        }
    }

    void AtttributeModel::loadAttributeData(unsigned long long attributeDataPtr) {
        if(attributeDataPtr == 0) {
            //clearData();
            beginResetModel();
            atttributeList.clear();
            summaryName.clear();
            summaryData.clear();
            processesName.clear();
            processesData.clear();
            properties.clear();
            endResetModel();
            return;
        }

        //todo: get attribute data from worker Interface

        refreshAtttibuteData();
    }

    void AtttributeModel::updateSelectedTask(const OperateData &opData, const QString &name) {
        currentTaskData = opData;
        currentTaskName = name;
        hasSelectedTask = true;

        beginResetModel();
        atttributeList.clear();

        // 添加基本任务属性
        int64_t duration = currentTaskData.end - static_cast<int64_t>(currentTaskData.start);

        atttributeList.append({"名称", currentTaskName});
        atttributeList.append({"操作ID", QString::number(currentTaskData.opId)});
        atttributeList.append({"开始时间", QString::number(currentTaskData.start) + " ms"});
        atttributeList.append({"结束时间", QString::number(currentTaskData.end) + " ms"});
        atttributeList.append({"持续时间", QString::number(duration) + " ms"});
        atttributeList.append({"任务类型", QString::number(currentTaskData.type)});

        // 如果有扩展属性数据，添加到列表
        if (isOpSelected) {
            for(size_t i = 0; i < processesName.size(); ++i) {
                const auto& nameData = processesName.at(i);
                QString key = QString::number(nameData.name);
                QVariant value = QVariant(/* todo: get data from worker */);
                atttributeList.append({key, value});
            }
        }

        endResetModel();
    }
} // isp