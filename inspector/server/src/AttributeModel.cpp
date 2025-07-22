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

#include "AttributeModel.h"
#include <QRegularExpression>

namespace inspector {
AttributeModel::AttributeModel(Worker* worker, ViewData* viewData, QObject* parent)
    : QObject(parent), worker(worker), viewData(viewData) {
  refreshData();
}

AttributeModel::~AttributeModel() {
  clearItems();
}

void AttributeModel::clearItems() {
  summaryItems.clear();
  colorProceses.clear();
  coverageProceses.clear();
}

bool AttributeModel::getIsTask() const {
  auto selectOpTask = viewData->selectOpTask;
  if (selectOpTask == -1) {
    return true;
  }
  const auto& dataContext = worker->getDataContext();
  const auto& opTasks = dataContext.opTasks;
  if (static_cast<uint32_t>(selectOpTask) > opTasks.size()) {
    return true;
  }
  const auto& opTask = opTasks[static_cast<uint32_t>(selectOpTask)];
  return getOpTaskType(OpTaskType(opTask->type)) == OpOrTask::Task;
}

QList<QObject*> AttributeModel::getSummaryItems() const {
  QList<QObject*> items;
  for (auto& item : summaryItems) {
    items.append(item.get());
  }
  return items;
}

QList<QObject*> AttributeModel::getColorProcessItems() const {
  QList<QObject*> items;
  for (auto& item : colorProceses) {
    items.append(item.get());
  }
  return items;
}

QList<QObject*> AttributeModel::getCoverageProcessItems() const {
  QList<QObject*> items;
  for (auto& item : coverageProceses) {
    items.append(item.get());
  }
  return items;
}

void AttributeModel::refreshData() {
  clearItems();
  auto selectOpTask = viewData->selectOpTask;
  if (selectOpTask == -1) {
    Q_EMIT itemsChanged();
    return;
  }
  const auto& dataContext = worker->getDataContext();
  const auto& properties = dataContext.properties;
  const auto& nameMap = dataContext.nameMap;
  const auto& propertyIter = properties.find(static_cast<uint32_t>(selectOpTask));
  if (propertyIter == properties.end()) {
    Q_EMIT itemsChanged();
    return;
  }
  const auto& propertyData = propertyIter->second;
  const auto& summaryName = propertyData->summaryName;
  auto& summaryData = propertyData->summaryData;
  summaryItems.reserve(summaryData.size());
  // 添加summary数据标题
  for (size_t i = 0; i < summaryData.size(); ++i) {
    const auto type = summaryName[i].type;
    auto name = QString("???");
    auto iter = nameMap.find(summaryName[i].name);
    if (iter != nameMap.end()) {
      name = iter->second.c_str();
    }
    auto data = summaryData[i];
    auto value = readData(type, std::move(data));
    summaryItems.push_back(std::make_shared<SummaryItem>(name, value));
  }
  Q_EMIT itemsChanged();
}

QVariant AttributeModel::readData(DataType type, std::shared_ptr<tgfx::Data> data) {
  if (data == nullptr) {
    return tr("nullptr(Parsing exception)");
  }
  auto dataView = tgfx::DataView(data->bytes(), data->size());
  switch (type) {
    case Color: {
      auto value = dataView.getUint32(0);
      uint8_t r = (value)&0xFF;
      uint8_t g = (value >> 8) & 0xFF;
      uint8_t b = (value >> 16) & 0xFF;
      uint8_t a = (value >> 24) & 0xFF;
      return "(" + getShowFloat(static_cast<float>(r) / 255.f) + ", " +
             getShowFloat(static_cast<float>(g) / 255.f) + ", " +
             getShowFloat(static_cast<float>(b) / 255.f) + ", " +
             getShowFloat(static_cast<float>(a) / 255.f) + ")";
    }
    case Vec4: {
      auto size = sizeof(float);
      auto data0 = dataView.getFloat(0);
      auto data1 = dataView.getFloat(size);
      auto data2 = dataView.getFloat(size * 2);
      auto data3 = dataView.getFloat(size * 3);
      return "(" + getShowFloat(data0) + ", " + getShowFloat(data1) + ", " + getShowFloat(data2) +
             ", " + getShowFloat(data3) + ")";
    }
    case Mat4: {
      auto size = sizeof(float);
      auto data0 = dataView.getFloat(0);
      auto data1 = dataView.getFloat(size);
      auto data2 = dataView.getFloat(size * 2);
      auto data3 = dataView.getFloat(size * 3);
      auto data4 = dataView.getFloat(size * 4);
      auto data5 = dataView.getFloat(size * 5);
      return "(" + getShowFloat(data0) + ", " + getShowFloat(data1) + ", " + getShowFloat(data2) +
             ", " + getShowFloat(data3) + ", " + getShowFloat(data4) + ", " + getShowFloat(data5) +
             ")";
    }
    case Int: {
      auto value = dataView.getInt32(0);
      return value;
    }
    case Uint32: {
      auto value = dataView.getUint32(0);
      return value;
    }
    case Bool: {
      auto value = dataView.getBoolean(0);
      return value ? tr("true") : tr("false");
    }
    case Float: {
      auto value = dataView.getFloat(0);
      return getShowFloat(value);
    }
    case Enum: {
      auto typeValue = dataView.getUint16(0);
      uint8_t enumType = (typeValue >> 8) & 0xFF;
      uint8_t enumValue = typeValue & 0xFF;
      auto enumTypeIter = TGFXEnumName.find((TGFXEnum)enumType);
      if (enumTypeIter == TGFXEnumName.end() || enumValue < 0 ||
          static_cast<size_t>(enumValue) >= enumTypeIter->second.size()) {
        return tr("???");
      }
      auto enumName = enumTypeIter->second[enumValue];
      return enumName.c_str();
    }
    default:
      return tr("nullptr(Parsing exception)");
  }
}

QString AttributeModel::getShowFloat(float data) {
  static auto regex = QRegularExpression("\\.?0+$");
  auto strData = QString::number(data, 'f', 2);
  return strData.remove(regex);
}
}  // namespace inspector