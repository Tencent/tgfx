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
#include <QWidget>
#include "FramesView.h"
#include "TracyFileRead.hpp"
#include "TracyWorker.hpp"
#include "src/profiler/TracyConfig.hpp"

class View : public QWidget{
public:
  View(const char* addr, uint16_t port, const tracy::Config& config, int width, QWidget* parent = nullptr);
  View(tracy::FileRead& file, int width, QWidget* parent = nullptr);

  void initView();
  void ViewImpl();
private:
  tracy::Worker worker;

  int width;
  const tracy::FrameData* frames;

  FramesView* framesView;
};