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
#include "Element.h"
#include "tgfx/layers/Layer.h"

class LayerUtils {
 public:
  static void UpdateCanvasMatrix(tgfx::Layer* layer, const JsRect& canvasRect,
                                 const JsRect& viewBox);

  static void MoveShape(tgfx::Layer* layer, const std::vector<JsElement>& elements);

  static bool MoveShape(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg);

  static void UpdateShape(tgfx::Layer* layer, const std::vector<JsElement>& elements);

  static bool UpdateShape(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg);

  static size_t CountLayers(const std::shared_ptr<tgfx::Layer>& layer);

  static void SetTypeface(const std::shared_ptr<tgfx::Typeface>& typeface);

 private:
  static bool UpdateRect(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg);

  static bool UpdateCircle(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg);

  static bool UpdateText(tgfx::Layer* layer, const JsElement& element, std::string& errorMsg);

  template <typename T>
  static std::shared_ptr<T> GetOrCreateLayer(tgfx::Layer* layer, const JsElement& element);

  static std::shared_ptr<tgfx::Typeface> currentTypeface;
};
