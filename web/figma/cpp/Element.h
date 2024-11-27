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
#include <string>
#include <vector>

struct JsText {
    float x = 0.0f;
    float y = 0.0f;
    float fontSize = 0.0f;
    std::string textContent;
};

struct JsRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct JsCircle {
    float cx = 0.0f;
    float cy = 0.0f;
    float r = 0.0f;
};

struct JsElement {
    std::string tagName;
    std::string id;
    JsRect rect;
    JsCircle circle;
    JsText text;
    std::string fill;
    std::string className;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

struct JsMessage {
    std::string action;
    JsRect canvasRect;
    JsRect viewBox;
    std::vector<JsElement> elements;
};
