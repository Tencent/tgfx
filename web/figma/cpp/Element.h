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
