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
#include <QGraphicsView>
#include "TimelineItem.h"

class TimelineItemThread: TimelineItem {
public:
  TimelineItemThread(View& view, tracy::Worker& worker, const tracy::ThreadData* key);

  void preprocess(const TimelineContext& ctx, tracy::TaskDispatch& td, bool visible, int yPos) override;
protected:
  void drawContent(const TimelineContext& ctx, int& offset) override;

  bool showFull;
private:

};