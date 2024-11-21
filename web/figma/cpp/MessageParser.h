
#pragma once
#include <string>
#include "Element.h"
#include <nlohmann/json.hpp>

class MessageParser {
public:
    static bool parseMessage(const std::string& message, JsMessage& jsMessage);
};