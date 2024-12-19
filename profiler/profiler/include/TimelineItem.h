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

#include <TracyTaskDispatch.hpp>
#include "TimelineContext.h"
#include "View.h"

class TimelineItem {
public:
  TimelineItem(View& view, tracy::Worker& worker, const void* key);
  virtual ~TimelineItem() = default;

  void draw(bool firstFrame, const TimelineContext ctx, int yOffset);
  virtual void preprocess(const TimelineContext& ctx, tracy::TaskDispatch& td, bool visible, int yPos) = 0;

protected:
  virtual void drawContent(const TimelineContext& ctx, int& offset) = 0;
  virtual void drawFinished() {}
private:
  const void* key;
protected:
  View& view;
  tracy::Worker& worker;
};