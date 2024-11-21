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