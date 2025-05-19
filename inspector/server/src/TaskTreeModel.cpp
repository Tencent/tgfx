

#include "TaskTreeModel.h"
#include <QRegularExpression>
#include "AtttributeModel.h"

namespace isp {
    TaskTreeModel::TaskTreeModel(QObject *parent) : QAbstractItemModel(parent) {
    }

    TaskTreeModel::~TaskTreeModel() {
        delete rootItem;
    }

    QHash<int, QByteArray> TaskTreeModel::roleNames() const {
        QHash<int, QByteArray> roles;
        roles[NameRole] = "name";
        roles[StartTimeRole] = "startTime";
        roles[EndTimeRole] = "endTime";
        roles[DurationRole] = "duration";
        roles[TypeRole] = "type";
        roles[OpIdRole] = "opId";
        roles[TaskIndexRole] = "taskIndex";
        return roles;

    }

    QVariant TaskTreeModel::data(const QModelIndex& index, int role) const {
        if(!index.isValid() || !rootItem) {
            return QVariant();
        }

        TaskItem* item = static_cast<TaskItem*>(index.internalPointer());

        switch (role) {
            case NameRole:
                return item->getName();
            case StartTimeRole:
                return QVariant(static_cast<qint64>(item->getOpData().start));
            case EndTimeRole:
                return QVariant(static_cast<qint64>(item->getOpData().end));
            case DurationRole:
                return QVariant(static_cast<qint64>(item->getOpData().end) - static_cast<qint64>(item->getOpData().start));
            case TypeRole:
                return QVariant(static_cast<uint>(item->getType()));
            case OpIdRole:
                return QVariant(static_cast<uint>(item->getOpId()));
            case TaskIndexRole: {
                const uint32_t opId = item->getOpId();
                for(size_t i = 0; i < opTask.size(); ++i) {
                    if(opTask.at(i).opId == opId) {
                        return QVariant(static_cast<int>(i));

                    }

                }
                return QVariant(-1);
            }

            default:
                return QVariant();
        }
    }

    QModelIndex TaskTreeModel::index(int row, int column, const QModelIndex &parent) const {
        if(!hasIndex(row, column, parent)) {
            return QModelIndex();
        }

        TaskItem* parentItem;
        if(!parent.isValid()) {
            parentItem = rootItem;
        }
        else {
            parentItem = static_cast<TaskItem*>(parent.internalPointer());
        }

        TaskItem* childItem = parentItem->childAt(row);
        if(childItem) {
            return createIndex(row, column, childItem);
        }
        return QModelIndex();

    }

    QModelIndex TaskTreeModel::parent(const QModelIndex &index) const {
        if(!index.isValid()) {
            return QModelIndex();
        }

        TaskItem* childItem = static_cast<TaskItem*>(index.internalPointer());
        TaskItem* parentItem = childItem->getParentItem();

        if(parentItem == rootItem || parentItem == nullptr) {
            return QModelIndex();
        }
        return createIndex(parentItem->row(), 0, parentItem);
    }

    int TaskTreeModel::rowCount(const QModelIndex& parent) const {
        if(parent.column() > 0) {
            return 0;
        }

        TaskItem* parentItem;
        if(!parent.isValid()) {
            parentItem = rootItem;
        }
        else {
            parentItem = static_cast<TaskItem*>(parent.internalPointer());
        }

        return parentItem ? parentItem->childCount() : 0;

    }

    int TaskTreeModel::columnCount(const QModelIndex& parent) const {
        Q_UNUSED(parent)
        return 1;
    }

    void TaskTreeModel::getTaskData(const std::vector<FrameData> &frames, const std::vector<OperateData> &opTasks,
        QStringList &typeNames, int startFrameIndex, int endFrameIndex) {
        beginResetModel();

        opTask = opTasks;
        typeName = typeNames;

        delete rootItem;
        rootItem = nullptr;
        opIdNodeMap.clear();

        if(startFrameIndex < 0 || endFrameIndex >= static_cast<int>(frames.size()) || startFrameIndex > endFrameIndex) {
            endResetModel();
            return;
        }

        QSet<int> taskIndex = getTaskIndexInRange(frames, opTasks, startFrameIndex, endFrameIndex);
        if(taskIndex.isEmpty()) {
            endResetModel();
            return;
        }

        rootItem = buildTaskTree(opTasks, typeNames, taskIndex);
        createOpIdNode(rootItem);

        endResetModel();
    }

    QModelIndex TaskTreeModel::getIndexByopId(uint32_t opId) const {
        TaskItem* item = findNodeByOpId(opId);
        if(!item) {
            return QModelIndex();
        }

        return createIndex(item->row(), 0, item);
    }


    TaskItem* TaskTreeModel::buildTaskTree(const std::vector<OperateData>& opTasks,
                                           const QStringList& typeName, const QSet<int>& taskIndex) {
        if(taskIndex.isEmpty() && opTasks.empty()) {
            return nullptr;
        }

        QList<int> sortedIndex = taskIndex.values();
        std::sort(sortedIndex.begin(), sortedIndex.end(), [&opTasks](size_t a, size_t b) {
            return opTasks.at(a).start < opTasks.at(b).start;
        });

        OperateData rootOp = {0, 0 , 0, 0 };
        TaskItem* rootItem = new TaskItem(rootOp, "Root");

        QMap<int, TaskItem*> indexToNodeMap;
        QMap<uint32_t, TaskItem*> opIdToTaskItemMap;

        for(const auto& idx : sortedIndex) {
            const OperateData& op = opTasks.at(static_cast<size_t>(idx));

            QString taskName;
            if(op.type < static_cast<uint8_t>(typeName.size())) {
                taskName = typeName.at(op.type);
            }
            else {
                taskName = "Type_" + QString::number(op.type);
            }

            if(!filterEnabled) {
                bool typeMatch = typeFilter.isEmpty() || typeFilter.contains(taskName);
                bool textMatch = textFilter.isEmpty() || taskName.contains(textFilter, Qt::CaseInsensitive);
                if(!typeMatch || !textMatch) {
                    continue;
                }
            }

            TaskItem* parentItem = findParentForTask(op, opIdToTaskItemMap,rootItem);
            TaskItem* newItem = new TaskItem(op, taskName, parentItem);

            parentItem->appendChild(newItem);

            indexToNodeMap[idx] = newItem;
            opIdToTaskItemMap[op.opId] = newItem;
        }

        if(rootItem->childCount() == 0) {
            delete rootItem;
            return nullptr;
        }
        return rootItem;
    }

    void TaskTreeModel::createOpIdNode(TaskItem* root, bool clearFirst) {
        if(clearFirst) {
            opIdNodeMap.clear();
        }

        if(!root) {
            return;
        }

        opIdNodeMap[root->getOpId()] = root;
        for(int i =0; i < root->childCount(); ++i) {
            createOpIdNode(root->childAt(i), false);
        }
    }

    TaskItem * TaskTreeModel::findNodeByOpId(uint32_t opId) const {
        return opIdNodeMap.value(opId, nullptr);
    }


    TaskItem * TaskTreeModel::findParentForTask(const OperateData& task,
                                                const QMap<uint32_t, TaskItem *> &opIdMap, TaskItem* rootItem) const {
        TaskItem* parentItem = rootItem;
        uint32_t parentStartTime = 0;

        for(auto it = opIdMap.begin(); it != opIdMap.end(); ++it) {
            TaskItem* potentialparent = it.value();
            if(static_cast<uint64_t>(potentialparent->startTime()) <= task.start &&
               static_cast<uint64_t>(potentialparent->endTime()) >= static_cast<uint64_t>(task.end)) {
                if(potentialparent->startTime() >= parentStartTime) {
                    parentItem = potentialparent;
                    parentStartTime = static_cast<uint32_t>(potentialparent->startTime());
                }
            }
        }
        return parentItem;
    }

    QSet<int> TaskTreeModel::getTaskIndexInRange(const std::vector<FrameData>& frames,
                                                 const std::vector<OperateData>& opTask, int startFrameIndex, int endFrameIndex) const {
        QSet<int> taskIndex;
        uint64_t startTime = 0;
        uint64_t endTime = UINT64_MAX;

        if(!frames.empty()) {
            startTime = static_cast<uint64_t>(frames.at(static_cast<size_t>(startFrameIndex)).start);
            endTime = static_cast<uint64_t>(frames.at(static_cast<size_t>(endFrameIndex)).end);
        }

        for(size_t i = 0; i < opTask.size(); ++i) {
            const OperateData& opData = opTask.at(i);
            if(static_cast<uint64_t>(opData.end) >= startTime && opData.start <= endTime) {
                taskIndex.insert(static_cast<int>(i));
            }
        }
        return taskIndex;
    }

    bool TaskTreeModel::matchesFilter(const QString &name) const {
        if(!typeFilter.isEmpty() && !typeFilter.contains(name)) {
            return false;
        }

        if(!textFilter.isEmpty()) {
            QRegularExpression regex(textFilter, QRegularExpression::CaseInsensitiveOption);
            return regex.match(name).hasMatch();
        }

        return true;
    }

    void TaskTreeModel::createTestData() {
        beginResetModel();
        // 清除现有数据
        delete rootItem;
        rootItem = nullptr;
        opIdNodeMap.clear();
        opTask.clear();
        frames.clear();

        // 创建测试的类型名称列表
        typeName = {
            "Flush",                    // 0 - 根节点
            "TextureFlatten",           // 1 - 无子节点
            "ResourceTask",             // 2 - 资源任务
            "TextureUploadTask",        // 3 - ResourceTask的子任务
            "RenderTargetCreateTask",   // 4 - ResourceTask的子任务
            "TextureCreateTask",        // 5 - ResourceTask的子任务
            "GpuBufferUploadTask",     // 6 - ResourceTask的子任务
            "ShapeBufferUploadTask",    // 7 - ResourceTask的子任务
            "RenderTask",               // 8 - 渲染任务
            "RenderTargetCopyTask",     // 9 - RenderTask的子任务
            "RunTimeDrawTask",         // 10 - RenderTask的子任务
            "TextureResolveTask",      // 11 - RenderTask的子任务
            "OpsRenderTask",           // 12 - RenderTask的子任务
            "ClearOp",                 // 13 - OpsRenderTask的子任务
            "ShapeDrawOp",            // 14 - OpsRenderTask的子任务
            "DrawOp",                 // 15 - OpsRenderTask的子任务
            "RectDrawOp"              // 16 - OpsRenderTask的子任务
        };

        // 创建测试帧数据
        FrameData frame;
        frame.start = 0;
        frame.end = 1000000; // 1秒时间跨度
        frame.drawcall = 50;
        frame.triangles = 10000;
        frames.push_back(frame);

        // 创建层级化的操作数据
        uint32_t nextOpId = 1;

        // 创建根节点任务 - Flush
        OperateData rootOp;
        rootOp.start = 0;
        rootOp.end = 1000000;
        rootOp.type = 0;      // Flush
        rootOp.opId = nextOpId++;
        opTask.push_back(rootOp);

        // 创建TextureFlatten任务 (无子节点)
        OperateData textureFlattenOp;
        textureFlattenOp.start = 10000;
        textureFlattenOp.end = 50000;
        textureFlattenOp.type = 1;  // TextureFlatten
        textureFlattenOp.opId = nextOpId++;
        opTask.push_back(textureFlattenOp);

        // 创建ResourceTask及其子任务
        OperateData resourceTaskOp;
        resourceTaskOp.start = 60000;
        resourceTaskOp.end = 300000;
        resourceTaskOp.type = 2;  // ResourceTask
        resourceTaskOp.opId = nextOpId++;
        opTask.push_back(resourceTaskOp);

        // ResourceTask的子任务
        std::vector<uint8_t> resourceSubTypes = {3, 4, 5, 6, 7}; // 对应各种上传/创建任务
        for (size_t i = 0; i < resourceSubTypes.size(); i++) {
            OperateData subTask;
            subTask.start = resourceTaskOp.start + (i * 50000);
            subTask.end = static_cast<int64_t>(subTask.start + 40000);
            subTask.type = resourceSubTypes.at(i);
            subTask.opId = nextOpId++;
            opTask.push_back(subTask);
        }

        // 创建RenderTask及其子任务
        OperateData renderTaskOp;
        renderTaskOp.start = 350000;
        renderTaskOp.end = 900000;
        renderTaskOp.type = 8;  // RenderTask
        renderTaskOp.opId = nextOpId++;
        opTask.push_back(renderTaskOp);


        std::vector<uint8_t> renderSubTypes = {9, 10, 11, 12};
        for (size_t i = 0; i < renderSubTypes.size(); i++) {
            OperateData subTask;
            subTask.start = renderTaskOp.start + (i * 150000);
            subTask.end = static_cast<int64_t>(subTask.start + 100000);
            subTask.type = renderSubTypes.at(i);
            subTask.opId = nextOpId++;
            opTask.push_back(subTask);

            // 如果是OpsRenderTask，添加其子任务
            if (subTask.type == 12) {
                std::vector<uint8_t> opsSubTypes = {13, 14, 15, 16}; // 对应各种绘制操作
                for (size_t j = 0; j < opsSubTypes.size(); j++) {
                    OperateData opsTask;
                    opsTask.start = subTask.start + (j * 30000);
                    opsTask.end = static_cast<int64_t>(opsTask.start + 25000);
                    opsTask.type = opsSubTypes.at(i);
                    opsTask.opId = nextOpId++;
                    opTask.push_back(opsTask);
                }
            }
        }

        QSet<int> allIndex;
        for (size_t i = 0; i < opTask.size(); i++) {
            allIndex.insert(static_cast<int>(i));
        }

        rootItem = buildTaskTree(opTask, typeName, allIndex);
        if (rootItem) {
            createOpIdNode(rootItem);
        }

        endResetModel();
        Q_EMIT filterChanged(); // 通知视图更新
    }

    void TaskTreeModel::selectedTask(const QModelIndex& index) {
        if(!index.isValid() || !rootItem) {
            return;
        }

        TaskItem* item = static_cast<TaskItem*>(index.internalPointer());
        if(item) {
            Q_EMIT taskSelected(item->getOpData(), item->getName(), item->getOpId());
        }
    }

    void TaskTreeModel::setAttributeModel(AtttributeModel* model) {
        if(model && model != atttributeModel) {
            atttributeModel = model;
            connect(this, &TaskTreeModel::taskSelected, atttributeModel, &AtttributeModel::updateSelectedTask);
        }
    }

    void TaskTreeModel::setTypeFilter(const QStringList& types) {
        if(typeFilter != types) {
            beginResetModel();
            typeFilter = types;
            filterEnabled = !typeFilter.isEmpty() || !textFilter.isEmpty();

            if(!opTask.empty()) {
                QSet<int> allIndex;
                for(size_t i = 0; i < opTask.size(); ++i) {
                    allIndex.insert(static_cast<int>(i));
                }

                delete rootItem;
                rootItem = buildTaskTree(opTask, typeName, allIndex);
                createOpIdNode(rootItem);
            }
            endResetModel();
            Q_EMIT filterChanged();
        }
    }

    void TaskTreeModel::setTextFilter(const QString &text) {
        if(textFilter != text) {
            beginResetModel();
            textFilter = text;
            filterEnabled = !textFilter.isEmpty() || !typeFilter.isEmpty();

            if(opTask.size() > 0 ) {
                QSet<int> allIndex;
                for(size_t i = 0; i < opTask.size(); ++i) {
                    allIndex.insert(static_cast<int>(i));
                }

                delete rootItem;
                rootItem = buildTaskTree(opTask, typeName, allIndex);
                createOpIdNode(rootItem);
            }
            endResetModel();
            Q_EMIT filterChanged();
        }
    }

    void TaskTreeModel::clearTypeFilter() {
        if(!typeFilter.isEmpty()) {
            beginResetModel();
            typeFilter.clear();
            filterEnabled = !typeFilter.isEmpty() || !textFilter.isEmpty();
            if(opTask.size() > 0 ) {
                QSet<int> allIndex;
                for(size_t i = 0; i < opTask.size(); ++i) {
                    allIndex.insert(static_cast<int>(i));
                }

                delete rootItem;
                rootItem = buildTaskTree(opTask, typeName, allIndex);
                createOpIdNode(rootItem);
            }
            endResetModel();
            Q_EMIT filterChanged();
        }
    }

    void TaskTreeModel::clearTextFilter() {
        if(!textFilter.isEmpty()) {
            beginResetModel();
            textFilter.clear();
            filterEnabled = !textFilter.isEmpty() || !typeFilter.isEmpty();
            if(opTask.size() > 0 ) {
                QSet<int> allIndex;
                for(size_t i = 0; i < opTask.size(); ++i) {
                    allIndex.insert(static_cast<int>(i));
                }

                delete rootItem;
                rootItem = buildTaskTree(opTask, typeName, allIndex);
                createOpIdNode(rootItem);
            }
            endResetModel();
            Q_EMIT filterChanged();
        }
    }

}

