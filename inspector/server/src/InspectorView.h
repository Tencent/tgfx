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

#include <QMainWindow>
#include <QQuickWindow>
#include "SelectFrameModel.h"
#include "StartView.h"
#include "TaskFilterModel.h"
#include "TaskTreeModel.h"
#include "ViewData.h"
#include "Worker.h"

namespace inspector {
class ClientData;
class InspectorView : public QObject {
  Q_OBJECT

 public:
  InspectorView(std::string filePath, int width, QObject* parent = nullptr);
  InspectorView(ClientData* clientData, int width, QObject* parent = nullptr);
  ~InspectorView();

  void initView();
  void initConnect();
  Q_INVOKABLE void openStartView();

  Q_SLOT void onCloseView(QQuickCloseEvent*);
  Q_SIGNAL void closeView(QObject* view);
 private:
  int width;
  ClientData* clientData = nullptr;
  Worker worker;
  ViewData viewData;
  std::unique_ptr<QQmlApplicationEngine> ispEngine = nullptr;
  std::unique_ptr<TaskTreeModel> taskTreeModel;
  std::unique_ptr<SelectFrameModel> selectFrameModel;
  std::unique_ptr<TaskFilterModel> taskFilterModel;
};
}  // namespace inspector
