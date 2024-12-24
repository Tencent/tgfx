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
#include "Utility.h"

struct ViewData {
  int64_t zvStart = 0;
  int64_t zvEnd = 0;
  int32_t frameScale = 0;
  int32_t frameStart = 0;

  uint8_t drawGpuZones = true;
  uint8_t drawZones = true;
  uint8_t drawLocks = true;
  uint8_t drawPlots = true;
  uint8_t onlyContendedLocks = true;
  uint8_t drawEmptyLabels = false;
  uint8_t drawFrameTargets = false;
  uint8_t drawContextSwitches = true;
  uint8_t darkenContextSwitches = true;
  uint8_t drawCpuData = true;
  uint8_t drawCpuUsageGraph = true;
  uint8_t drawSamples = true;
  uint8_t dynamicColors = 1;
  uint8_t inheritParentColors = true;
  uint8_t forceColors = false;
  uint8_t ghostZones = true;
  ShortenName shortenName = ShortenName::NoSpaceAndNormalize;

  uint32_t frameTarget = 60;

  uint32_t plotHeight = 100;
};