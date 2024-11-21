
#include "MessageParser.h"
#include <iostream>

bool MessageParser::parseMessage(const std::string& message, JsMessage& jsMessage) {
    try {
        nlohmann::json jsonMsg = nlohmann::json::parse(message);
        
        if(jsonMsg.contains("action")){
            jsMessage.action = jsonMsg["action"].get<std::string>();
        }

        if(jsonMsg.contains("canvasRect")){
            if(jsonMsg["canvasRect"].contains("x")) jsMessage.canvasRect.x = jsonMsg["canvasRect"]["x"].get<float>();
            if(jsonMsg["canvasRect"].contains("y")) jsMessage.canvasRect.y = jsonMsg["canvasRect"]["y"].get<float>();
            if(jsonMsg["canvasRect"].contains("width")) jsMessage.canvasRect.width = jsonMsg["canvasRect"]["width"].get<float>();
            if(jsonMsg["canvasRect"].contains("height")) jsMessage.canvasRect.height = jsonMsg["canvasRect"]["height"].get<float>();
        }

        if(jsonMsg.contains("viewBox")){
            if(jsonMsg["viewBox"].contains("x")) jsMessage.viewBox.x = jsonMsg["viewBox"]["x"].get<float>();
            if(jsonMsg["viewBox"].contains("y")) jsMessage.viewBox.y = jsonMsg["viewBox"]["y"].get<float>();
            if(jsonMsg["viewBox"].contains("width")) jsMessage.viewBox.width = jsonMsg["viewBox"]["width"].get<float>();
            if(jsonMsg["viewBox"].contains("height")) jsMessage.viewBox.height = jsonMsg["viewBox"]["height"].get<float>();
        }

        if(jsonMsg.contains("elements")){
            for(auto& elem : jsonMsg["elements"]){
                JsElement jsElem;
                if(elem.contains("tagName")) jsElem.tagName = elem["tagName"].get<std::string>();
                if(elem.contains("id")) jsElem.id = elem["id"].get<std::string>();
                if(elem.contains("rect")){
                    if(elem["rect"].contains("x")) jsElem.rect.x = elem["rect"]["x"].get<float>();
                    if(elem["rect"].contains("y")) jsElem.rect.y = elem["rect"]["y"].get<float>();
                    if(elem["rect"].contains("width")) jsElem.rect.width = elem["rect"]["width"].get<float>();
                    if(elem["rect"].contains("height")) jsElem.rect.height = elem["rect"]["height"].get<float>();
                }
                if(elem.contains("circle")){
                    if(elem["circle"].contains("cx")) jsElem.circle.cx = elem["circle"]["cx"].get<float>();
                    if(elem["circle"].contains("cy")) jsElem.circle.cy = elem["circle"]["cy"].get<float>();
                    if(elem["circle"].contains("r")) jsElem.circle.r = elem["circle"]["r"].get<float>();
                }
                if(elem.contains("text")){
                    if(elem["text"].contains("x")) jsElem.text.x = elem["text"]["x"].get<float>();
                    if(elem["text"].contains("y")) jsElem.text.y = elem["text"]["y"].get<float>();
                    if(elem["text"].contains("fontSize")) jsElem.text.fontSize = elem["text"]["fontSize"].get<float>();
                    if(elem["text"].contains("textContent")) jsElem.text.textContent = elem["text"]["textContent"].get<std::string>();
                }
                if(elem.contains("fill")) jsElem.fill = elem["fill"].get<std::string>();
                if(elem.contains("className")) jsElem.className = elem["className"].get<std::string>();
                if(elem.contains("offsetX")) jsElem.offsetX = elem["offsetX"].get<float>();
                if(elem.contains("offsetY")) jsElem.offsetY = elem["offsetY"].get<float>();
                jsMessage.elements.push_back(jsElem);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "JSON解析错误: " << e.what() << std::endl;
        return false;
    }
}