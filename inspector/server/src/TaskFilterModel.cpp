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

#include "TaskFilterModel.h"
#include <iostream>

namespace inspector {
TaskFilterItem::TaskFilterItem(const char* data, int32_t type, TaskFilterItem* parentItem)
    : type(type), itemData(data), parentItem(parentItem) {
}

void TaskFilterItem::appendChild(std::unique_ptr<TaskFilterItem>& child) {
  child->parentItem = this;
  childItems.push_back(std::move(child));
}

TaskFilterItem* TaskFilterItem::child(uint32_t row) {
  return row >= static_cast<uint32_t>(0) && row < childItems.size() ? childItems.at(row).get()
                                                                    : nullptr;
}

int TaskFilterItem::childCount() const {
  return int(childItems.size());
}

int TaskFilterItem::columnCount() const {
  return 1;
}

int TaskFilterItem::row() const {
  if (parentItem == nullptr) {
    return 0;
  }
  const auto it = std::find_if(
      parentItem->childItems.cbegin(), parentItem->childItems.cend(),
      [this](const std::unique_ptr<TaskFilterItem>& treeItem) { return treeItem.get() == this; });

  if (it != parentItem->childItems.cend()) {
    return int(std::distance(parentItem->childItems.cbegin(), it));
  }
  Q_ASSERT(false);  // should not happen
}

const std::string& TaskFilterItem::data() const {
  return itemData;
}

int32_t TaskFilterItem::childFilterType(int depth) const {
  if (depth == 0) {
    return 0;
  }
  --depth;
  int32_t resultFilterType = 0;
  for (const auto& item : childItems) {
    resultFilterType |= item->filterType();
    resultFilterType |= item->childFilterType(depth);
  }
  return resultFilterType;
}

int32_t TaskFilterItem::filterType() const {
  return type;
}

TaskFilterItem* TaskFilterItem::getParentItem() const {
  return parentItem;
}

TaskFilterModel::TaskFilterModel(ViewData* data, QObject* parent)
    : QAbstractItemModel(parent), viewData(data),
      rootItem(std::make_unique<TaskFilterItem>("typeName", 0)) {
  setUpModelData();
}

TaskFilterModel::~TaskFilterModel() = default;

int TaskFilterModel::getFilterType() const {
  return static_cast<int>(viewData->opTaskFilterType);
}

void TaskFilterModel::checkedItem(const QModelIndex& index, bool checked) {
  const auto item = static_cast<const TaskFilterItem*>(index.internalPointer());
  if (!item || item == rootItem.get()) {
    return;
  }
  const auto itemType = static_cast<uint32_t>(item->filterType());
  auto& filterType = viewData->opTaskFilterType;
  filterType |= itemType;
  if (!checked) {
    filterType ^= itemType;
  }
  checkedParentItem(item, checked);
  checkedChildrenItem(item, checked);
  Q_EMIT filterTypeChange();
}

void TaskFilterModel::setTextFilter(const QString& text) {
  viewData->opTaskFilterName = text.toStdString();
  Q_EMIT filterTypeChange();
}

void TaskFilterModel::checkedParentItem(const TaskFilterItem* item, bool checked) {
  auto& filterType = viewData->opTaskFilterType;
  const auto parentItem = item->getParentItem();
  if (!parentItem) {
    return;
  }

  const auto parentItemType = static_cast<uint32_t>(parentItem->filterType());
  const auto broItemType = static_cast<uint32_t>(parentItem->childFilterType(1));
  if (!checked && !(broItemType & filterType)) {
    filterType |= parentItemType;
    filterType ^= parentItemType;
  } else if (checked) {
    filterType |= parentItemType;
  }
  checkedParentItem(parentItem, checked);
}

void TaskFilterModel::checkedChildrenItem(const TaskFilterItem* item, bool checked) {
  auto& filterType = viewData->opTaskFilterType;
  const auto childrenItemType = static_cast<uint32_t>(item->childFilterType());
  filterType |= childrenItemType;
  if (!checked) {
    filterType ^= childrenItemType;
  }
}

QVariant TaskFilterModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || rootItem == nullptr) return {};

  const auto* item = static_cast<const TaskFilterItem*>(index.internalPointer());
  switch (role) {
    case NameRole:
      return item->data().c_str();
    case FilterTypeRole:
      return item->filterType();
    default:
      return "";
  }
}

QHash<int, QByteArray> TaskFilterModel::roleNames() const {
  QHash<int, QByteArray> names;
  names[NameRole] = "name";
  names[FilterTypeRole] = "filterType";
  return names;
}

QModelIndex TaskFilterModel::index(int row, int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) {
    return {};
  }

  auto parentItem =
      parent.isValid() ? static_cast<TaskFilterItem*>(parent.internalPointer()) : rootItem.get();

  if (auto* childItem = parentItem->child(static_cast<uint32_t>(row))) {
    return createIndex(row, column, childItem);
  }
  return {};
}

QModelIndex TaskFilterModel::parent(const QModelIndex& index) const {
  if (!index.isValid()) {
    return {};
  }

  auto* childItem = static_cast<TaskFilterItem*>(index.internalPointer());
  TaskFilterItem* parentItem = childItem->getParentItem();

  return parentItem != rootItem.get() ? createIndex(parentItem->row(), 0, parentItem)
                                      : QModelIndex{};
}

int TaskFilterModel::rowCount(const QModelIndex& parent) const {
  if (parent.column() > 0) {
    return 0;
  }

  auto parentItem = parent.isValid() ? static_cast<const TaskFilterItem*>(parent.internalPointer())
                                     : rootItem.get();

  return parentItem->childCount();
}

int TaskFilterModel::columnCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return static_cast<TaskFilterItem*>(parent.internalPointer())->columnCount();
  }
  return rootItem->columnCount();
}

void TaskFilterModel::setUpModelData() {
  auto flush = std::make_unique<TaskFilterItem>("Flush", 1 << 0);
  auto resourceTask = std::make_unique<TaskFilterItem>("ResourceTask", 1 << 1);
  auto textureUploadTask = std::make_unique<TaskFilterItem>("TextureUploadTask", 1 << 2);
  auto shapeBufferUploadTask = std::make_unique<TaskFilterItem>("ShapeBufferUploadTask", 1 << 3);
  auto gpuUploadTask = std::make_unique<TaskFilterItem>("GpuUploadTask", 1 << 4);
  auto textureCreateTask = std::make_unique<TaskFilterItem>("TextureCreateTask", 1 << 5);
  auto renderTargetCreateTask = std::make_unique<TaskFilterItem>("RenderTargetCreateTask", 1 << 6);
  auto textureFlattenTask = std::make_unique<TaskFilterItem>("TextureFlattenTask", 1 << 7);
  auto renderTask = std::make_unique<TaskFilterItem>("RenderTask", 1 << 8);
  auto renderTargetCopyTask = std::make_unique<TaskFilterItem>("RenderTargetCopyTask", 1 << 9);
  auto runtimeDrawTask = std::make_unique<TaskFilterItem>("RuntimeDrawTask", 1 << 10);
  auto textureResolveTask = std::make_unique<TaskFilterItem>("TextureResolveTask", 1 << 11);
  auto opsRenderTask = std::make_unique<TaskFilterItem>("OpsRenderTask", 1 << 12);
  auto clearOp = std::make_unique<TaskFilterItem>("ClearOp", 1 << 13);
  auto rectDrawOp = std::make_unique<TaskFilterItem>("RectDrawOp", 1 << 14);
  auto rRectDrawOp = std::make_unique<TaskFilterItem>("RRectDrawOp", 1 << 15);
  auto shapeDrawOp = std::make_unique<TaskFilterItem>("ShapeDrawOp", 1 << 16);
  auto dstTextureCopyOp = std::make_unique<TaskFilterItem>("DstTextureCopyOp", 1 << 17);
  auto resolveOp = std::make_unique<TaskFilterItem>("ResolveOp", 1 << 18);
  flush->appendChild(textureFlattenTask);

  resourceTask->appendChild(textureUploadTask);
  resourceTask->appendChild(shapeBufferUploadTask);
  resourceTask->appendChild(gpuUploadTask);
  resourceTask->appendChild(textureCreateTask);
  resourceTask->appendChild(renderTargetCreateTask);
  flush->appendChild(resourceTask);

  renderTask->appendChild(renderTargetCopyTask);
  renderTask->appendChild(runtimeDrawTask);
  renderTask->appendChild(textureResolveTask);

  opsRenderTask->appendChild(clearOp);
  opsRenderTask->appendChild(rectDrawOp);
  opsRenderTask->appendChild(rRectDrawOp);
  opsRenderTask->appendChild(shapeDrawOp);
  opsRenderTask->appendChild(dstTextureCopyOp);
  opsRenderTask->appendChild(resolveOp);

  renderTask->appendChild(opsRenderTask);
  flush->appendChild(renderTask);
  rootItem->appendChild(flush);
}

}  // namespace inspector