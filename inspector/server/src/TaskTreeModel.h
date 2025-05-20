#pragma once
#include <QAbstractItemModel>
#include <QQmlEngine>
#include "ViewData.h"


namespace isp {
    struct OperateData {
        uint64_t start;
        int64_t end;
        uint8_t type;
        uint32_t opId;
    };

    class AtttributeModel;
    class TaskItem {
    public:
        explicit TaskItem(const OperateData& _opData, const QString& name, TaskItem* parent = nullptr) : opData(_opData), opName(name), parentItem(parent) {}
        ~TaskItem() { qDeleteAll(childrenItems); }

        void appendChild(TaskItem* child) { return childrenItems.append(child); }
        TaskItem* childAt(int row) { return (row >= 0 && row < childrenItems.size()) ? childrenItems.at(row) : nullptr; }
        int childCount() const { return static_cast<int>(childrenItems.size()); }
        TaskItem* getParentItem() const { return parentItem; }
        const QString& getName() const { return opName; }
        const OperateData& getOpData() const { return opData; }
        int64_t startTime() const { return static_cast<int64_t>(opData.start); }
        int64_t endTime() const { return opData.end; }
        uint8_t getType() const { return opData.type; }
        uint32_t getOpId() const { return opData.opId; }

        int row() const {
            if(parentItem) {
                return static_cast<int>(parentItem->childrenItems.indexOf(const_cast<TaskItem*>(this)));
            }
            return 0;
        }

    private:
        OperateData opData;
        QString opName;
        TaskItem* parentItem;
        QVector<TaskItem*> childrenItems;
    };



    class TaskTreeModel : public QAbstractItemModel {
        Q_OBJECT
    public:
        enum Roles {
            NameRole = Qt::UserRole + 1,
            StartTimeRole,
            EndTimeRole,
            DurationRole,
            TypeRole,
            OpIdRole,
            TaskIndexRole
        };

        explicit TaskTreeModel(QObject* parent = nullptr);
        ~TaskTreeModel() override;

        ///* TreeModel *///
        QHash<int, QByteArray> roleNames() const override;
        QVariant data(const QModelIndex& index, int role) const override;
        QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
        QModelIndex parent(const QModelIndex& index) const override;
        int rowCount(const QModelIndex& parent ) const override;
        int columnCount(const QModelIndex& parent) const override;


        void getTaskData(const std::vector<FrameData>& frames ,const std::vector<OperateData>& opTasks, QStringList& typeNames,  int startFrameIndex, int endFrameIndex);
        QModelIndex getIndexByopId(uint32_t opId) const;

        ///* used to create load real data from worker *///
        ///* similar to refreshInstrumentData() *///
        Q_INVOKABLE void createTestData();
        Q_INVOKABLE void selectedTask(const QModelIndex& index);
        Q_INVOKABLE void setAttributeModel(AtttributeModel* model);

        ///* filter *///
        Q_INVOKABLE void setTypeFilter(const QStringList& types);
        Q_INVOKABLE void setTextFilter(const QString& text);
        Q_INVOKABLE void clearTypeFilter();
        Q_INVOKABLE void clearTextFilter();

        Q_SIGNAL void taskSelected(const OperateData& opData, const QString& name, uint32_t opId);
        Q_SIGNAL void filterChanged();

    protected:
        TaskItem* buildTaskTree(const std::vector<OperateData>& opTasks, const QStringList& typeName, const QSet<int>& taskIndex );
        void createOpIdNode(TaskItem* root, bool clearFirst = true);
        TaskItem* findNodeByOpId(uint32_t opId) const;
        TaskItem* findParentForTask(const OperateData& task, const QMap<uint32_t, TaskItem*>& opIdMap, TaskItem* rootItem) const;
        QSet<int> getTaskIndexInRange(const std::vector<FrameData>& frames,const std::vector<OperateData>& opTask, int startFrameIndex, int endFrameIndex) const;

        bool matchesFilter(const QString& name) const;

    private:
        AtttributeModel* atttributeModel = nullptr;
        std::vector<OperateData> opTask;
        std::vector<FrameData> frames;
        QStringList sourceLocationExpand;
        TaskItem* rootItem = nullptr;
        QStringList typeFilter;
        QStringList typeName;
        QString textFilter;
        bool filterEnabled = false;
        QMap<uint32_t, TaskItem*> opIdNodeMap;
    };

}





