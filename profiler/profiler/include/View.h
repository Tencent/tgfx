/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <QPushButton>
#include <QQmlApplicationEngine>
#include <QWidget>
#include "FramesView.h"
#include "TimelineController.h"
#include "TimelineView.h"
#include "TracyFileRead.hpp"
#include "TracyWorker.hpp"
#include "UserData.h"
#include "src/profiler/TracyConfig.hpp"
#include "src/profiler/TracyUserData.hpp"

class View : public QWidget{
public:
  View(const char* addr, uint16_t port, int width, const tracy::Config& config, QWidget* parent = nullptr);
  View(tracy::FileRead& file, int width, const tracy::Config& config, QWidget* parent = nullptr);

  ~View();

  ViewData& getViewData() {return viewData;}
  void initView();
  void ViewImpl();
private:
  int width;
  tracy::Worker worker;

  ViewData viewData;
  UserData userData;
  QQmlApplicationEngine* timelineEngine;
  QQmlApplicationEngine* framesEngine;

  const tracy::Config& config;
  FramesView* framesView;
  TimelineView* timelineView;
};