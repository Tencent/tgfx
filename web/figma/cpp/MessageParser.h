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
#include "Element.h"
#include <nlohmann/json.hpp>

class MessageParser {
public:
    static bool parseMessage(const std::string& message, JsMessage& jsMessage);
private:
    static void parseCanvasRect(const nlohmann::json& canvasRect, JsMessage& jsMessage);
    static void parseViewBox(const nlohmann::json& viewBox, JsMessage& jsMessage);
    static void parseElements(const nlohmann::json& elements, JsMessage& jsMessage);

    static void parseRect(const nlohmann::json& rect, JsElement& jsElem);
    static void parseCircle(const nlohmann::json& circle, JsElement& jsElem);
    static void parseText(const nlohmann::json& text, JsElement& jsElem);

    static float getFloat(const nlohmann::json& obj, const std::string& key);
};