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

#pragma once
#include <QAbstractItemModel>
#include "ViewData.h"
#include "Worker.h"

namespace inspector {
class SummaryItem : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString name READ getName CONSTANT)
  Q_PROPERTY(QVariant value READ getValue CONSTANT)
 public:
  SummaryItem(QString name, QVariant value, QObject* parent = nullptr)
      : QObject(parent), name(std::move(name)), value(std::move(value)) {
  }
  QString getName() const {
    return name;
  }
  QVariant getValue() const {
    return value;
  }

 private:
  QString name;
  QVariant value;
};

class ProcessItem : public QObject {
  Q_OBJECT
  Q_PROPERTY(int level READ getLevel CONSTANT)
  Q_PROPERTY(QString name READ getName CONSTANT)
 public:
  ProcessItem(QString name, int level, QObject* parent = nullptr)
      : QObject(parent), level(level), name(std::move(name)) {
  }
  int getLevel() const {
    return level;
  }
  QString getName() const {
    return name;
  }

 private:
  int level;
  QString name;
};

class AttributeModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool isTask READ getIsTask NOTIFY itemsChanged)
  Q_PROPERTY(QList<QObject*> summaryItems READ getSummaryItems NOTIFY itemsChanged)
  Q_PROPERTY(QList<QObject*> colorProcessItems READ getColorProcessItems NOTIFY itemsChanged)
  Q_PROPERTY(QList<QObject*> coverageProcessItems READ getCoverageProcessItems NOTIFY itemsChanged)
 public:
  explicit AttributeModel(Worker* worker, ViewData* viewData, QObject* parent = nullptr);
  ~AttributeModel() override;

  void clearItems();

  Q_INVOKABLE bool getIsTask() const;
  Q_INVOKABLE QList<QObject*> getSummaryItems() const;
  Q_INVOKABLE QList<QObject*> getColorProcessItems() const;
  Q_INVOKABLE QList<QObject*> getCoverageProcessItems() const;

  Q_SLOT void refreshData();

  Q_SIGNAL void opSelectedChanged();
  Q_SIGNAL void itemsChanged();

 protected:
  QVariant readData(DataType type, std::shared_ptr<tgfx::Data> data);
  QString getShowFloat(float data);

 private:
  Worker* worker = nullptr;
  ViewData* viewData = nullptr;
  std::vector<std::shared_ptr<SummaryItem>> summaryItems;
  std::vector<std::shared_ptr<ProcessItem>> colorProceses;
  std::vector<std::shared_ptr<ProcessItem>> coverageProceses;
};

}  // namespace inspector
