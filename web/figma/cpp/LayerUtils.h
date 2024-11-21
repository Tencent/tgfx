#pragma once
#include "tgfx/layers/Layer.h"
#include "Element.h"

class LayerUtils {
public:
    static void UpdateCanvasMatrix(tgfx::Layer *layer, const JsRect &canvasRect, const JsRect &viewBox);

    static void MoveShape(tgfx::Layer *layer, const std::vector<JsElement> &elements);

    static bool MoveShape(tgfx::Layer *layer, const JsElement &element, std::string &errorMsg);

    static void UpdateShape(tgfx::Layer *layer, const std::vector<JsElement> &elements);

    static bool UpdateShape(tgfx::Layer *layer, const JsElement &element, std::string &errorMsg);

    static size_t CountLayers(const std::shared_ptr<tgfx::Layer>& layer);

private:
    static bool UpdateRect(tgfx::Layer *layer, const JsElement &element, std::string &errorMsg);

    static bool UpdateCircle(tgfx::Layer *layer, const JsElement &element, std::string &errorMsg);

    static bool UpdateText(tgfx::Layer *layer, const JsElement &element, std::string &errorMsg);

    template <typename T>
    static std::shared_ptr<T> GetOrCreateLayer(tgfx::Layer* layer, const JsElement& element);
};
