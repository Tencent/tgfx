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
#include <memory>
#include <optional>
#include "TimelineItem.h"
#include "TimelineItemThread.h"

class View;

class TimelineController {
public:
  TimelineController(TimelineView& view, tracy::Worker& worker, bool threading);
  ~TimelineController();

  void firstFrameExpired();
  void begin();
  void end(double pxns, const QPointF& wpos, bool vcenter, float yMin, float yMax, QPainter* painter);

  template<class T, class U>
  void addItem(U* data) {
    auto it = itemMap.find(data);
    if (it == itemMap.end()) {
      it = itemMap.emplace(data, std::make_unique<T>(view, worker, data)).first;
    }
    items.emplace_back(it->second.get());
  }

  tracy_force_inline TimelineItem& getItem(const void* data){
    auto it = itemMap.find(data);
    assert(it != itemMap.end());
    return *it->second;
  }

private:
  std::vector<TimelineItem*> items;
  tracy::unordered_flat_map<const void*, std::unique_ptr<TimelineItem>> itemMap;
  bool firstFrame;
  TimelineView& view;
  tracy::Worker& worker;

  tracy::TaskDispatch taskDispatch;
};