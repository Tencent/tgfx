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

#include "MessageParser.h"
#include <iostream>

float MessageParser::getFloat(const nlohmann::json& obj, const std::string& key) {
  if (!obj.contains(key)) {
    // 键不存在，返回默认值
    return 0.0f;
  }

  const auto& value = obj[key];
  if (value.is_number_float()) {
    return value.get<float>();
  }
  if (value.is_number_integer()) {
    return static_cast<float>(value.get<int>());
  }
  if (value.is_string()) {
    return std::stof(value.get<std::string>());
  }
  // 无法转换，返回默认值
  return 0.0f;
}

bool MessageParser::parseMessage(const std::string& message, JsMessage& jsMessage) {
  try {
    nlohmann::json jsonMsg = nlohmann::json::parse(message);

    if (jsonMsg.contains("action")) {
      jsMessage.action = jsonMsg["action"].get<std::string>();
    }

    if (jsonMsg.contains("canvasRect")) {
      parseCanvasRect(jsonMsg["canvasRect"], jsMessage);
    }
    if (jsonMsg.contains("viewBox")) {
      parseViewBox(jsonMsg["viewBox"], jsMessage);
    }
    if (jsonMsg.contains("elements")) {
      parseElements(jsonMsg["elements"], jsMessage);
    }

    return true;
  } catch (const std::exception& e) {
    std::cerr << "JSON解析错误: " << e.what() << std::endl;
    return false;
  }
}

void MessageParser::parseCanvasRect(const nlohmann::json& canvasRect, JsMessage& jsMessage) {
  if (canvasRect.contains("x")) jsMessage.canvasRect.x = getFloat(canvasRect, "x");
  if (canvasRect.contains("y")) jsMessage.canvasRect.y = getFloat(canvasRect, "y");
  if (canvasRect.contains("width")) jsMessage.canvasRect.width = getFloat(canvasRect, "width");
  if (canvasRect.contains("height")) jsMessage.canvasRect.height = getFloat(canvasRect, "height");
}

void MessageParser::parseViewBox(const nlohmann::json& viewBox, JsMessage& jsMessage) {
  if (viewBox.contains("x")) jsMessage.viewBox.x = getFloat(viewBox, "x");
  if (viewBox.contains("y")) jsMessage.viewBox.y = getFloat(viewBox, "y");
  if (viewBox.contains("width")) jsMessage.viewBox.width = getFloat(viewBox, "width");
  if (viewBox.contains("height")) jsMessage.viewBox.height = getFloat(viewBox, "height");
}

void MessageParser::parseElements(const nlohmann::json& elements, JsMessage& jsMessage) {
  for (auto& elem : elements) {
    JsElement jsElem;
    if (elem.contains("tagName")) jsElem.tagName = elem["tagName"].get<std::string>();
    if (elem.contains("id")) jsElem.id = elem["id"].get<std::string>();
    if (elem.contains("fill")) jsElem.fill = elem["fill"].get<std::string>();
    if (elem.contains("class")) jsElem.className = elem["className"].get<std::string>();
    if (elem.contains("offsetX")) jsElem.offsetX = getFloat(elem, "offsetX");
    if (elem.contains("offsetY")) jsElem.offsetY = getFloat(elem, "offsetY");
    if (jsElem.tagName == "rect") {
      parseRect(elem, jsElem);
    }
    if (jsElem.tagName == "circle") {
      parseCircle(elem, jsElem);
    }
    if (jsElem.tagName == "text") {
      parseText(elem, jsElem);
    }
    jsMessage.elements.push_back(jsElem);
  }
}

void MessageParser::parseRect(const nlohmann::json& rect, JsElement& jsElem) {
  if (rect.contains("x")) jsElem.rect.x = getFloat(rect, "x");
  if (rect.contains("y")) jsElem.rect.y = getFloat(rect, "y");
  if (rect.contains("width")) jsElem.rect.width = getFloat(rect, "width");
  if (rect.contains("height")) jsElem.rect.height = getFloat(rect, "height");
}

void MessageParser::parseCircle(const nlohmann::json& circle, JsElement& jsElem) {

  if (circle.contains("cx")) jsElem.circle.cx = getFloat(circle, "cx");
  if (circle.contains("cy")) jsElem.circle.cy = getFloat(circle, "cy");
  if (circle.contains("r")) jsElem.circle.r = getFloat(circle, "r");
}

void MessageParser::parseText(const nlohmann::json& text, JsElement& jsElem) {
  if (text.contains("x")) jsElem.text.x = getFloat(text, "x");
  if (text.contains("y")) jsElem.text.y = getFloat(text, "y");
  if (text.contains("font-size")) jsElem.text.fontSize = getFloat(text, "font-size");
  if (text.contains("textContent"))
    jsElem.text.textContent = text["textContent"].get<std::string>();
 }
