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
#include "tgfx/layers/LayerInspector.h"
#ifdef TGFX_USE_INSPECTOR
#include "layers/LayerInspectorManager.h"
#endif

void tgfx::LayerInspector::pickedLayer(float x, float y) {
#ifdef TGFX_USE_INSPECTOR
  LayerInspectorManager::GetLayerInspectorManager().pickedLayer(x, y);
#else
  (void)x;
  (void)y;
#endif
}

void tgfx::LayerInspector::setLayerInspectorHoveredStateCallBack(
    std::function<void(bool)> callback) {
#ifdef TGFX_USE_INSPECTOR
  LayerInspectorManager::GetLayerInspectorManager().setLayerInspectorHoveredStateCallBack(callback);
#else
  (void)callback;
#endif
}
