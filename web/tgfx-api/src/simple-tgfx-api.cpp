/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Simple TGFX API Bindings for WebAssembly
//  简化版TGFX API WebAssembly绑定
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <emscripten/bind.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <emscripten/console.h>
#include "tgfx/core/Color.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"

using namespace emscripten;
using namespace tgfx;

// 全局错误信息
static std::string g_errorMessage = "";

// 颜色创建函数
Color MakeColor(float r, float g, float b, float a = 1.0f) {
    return Color(r, g, b, a);
}

// 矩形创建函数
Rect MakeRect(float left, float top, float right, float bottom) {
    return Rect::MakeLTRB(left, top, right, bottom);
}

// 点创建函数
Point MakePoint(float x, float y) {
    return Point::Make(x, y);
}

// 全局变量管理Surface和Context
static std::shared_ptr<WebGLWindow> g_window = nullptr;
static std::shared_ptr<Surface> g_surface = nullptr;
static Canvas* g_canvas = nullptr;
static std::shared_ptr<Context> g_context = nullptr;

// 初始化WebGL和TGFX
bool InitializeTGFX(const std::string& canvasId) {
    try {
        g_errorMessage = "";
        emscripten_console_log(("InitializeTGFX: 开始初始化，canvasId: " + canvasId).c_str());

        // 简化的调试信息
        emscripten_console_log(("正在尝试使用Canvas ID: " + canvasId).c_str());

        // 创建WebGL Window
        g_window = WebGLWindow::MakeFrom(canvasId);
        if (!g_window) {
            g_errorMessage = "创建WebGL Window失败 - 可能是WebGL上下文问题或Canvas不存在";
            emscripten_console_error(g_errorMessage.c_str());
            return false;
        }
        emscripten_console_log("InitializeTGFX: WebGL Window创建成功");

        // 获取Device和Context
        auto device = g_window->getDevice();
        if (!device) {
            g_errorMessage = "获取Device失败";
            emscripten_console_error(g_errorMessage.c_str());
            return false;
        }
        emscripten_console_log("InitializeTGFX: Device获取成功");

        auto context = device->lockContext();
        if (!context) {
            g_errorMessage = "锁定Context失败";
            emscripten_console_error(g_errorMessage.c_str());
            return false;
        }
        emscripten_console_log("InitializeTGFX: Context锁定成功");

        g_context = std::shared_ptr<Context>(context, [device](Context*) {
            // 使用自定义删除器来调用device->unlock()
            device->unlock();
        });

        // 获取Surface
        g_surface = g_window->getSurface(context);
        if (!g_surface) {
            g_errorMessage = "获取Surface失败";
            emscripten_console_error(g_errorMessage.c_str());
            return false;
        }
        emscripten_console_log("InitializeTGFX: Surface获取成功");

        // 获取Canvas
        g_canvas = g_surface->getCanvas();
        if (!g_canvas) {
            g_errorMessage = "获取Canvas失败";
            emscripten_console_error(g_errorMessage.c_str());
            return false;
        }
        emscripten_console_log("InitializeTGFX: Canvas获取成功，初始化完成");
        return true;

    } catch (const std::exception& e) {
        g_errorMessage = std::string("异常: ") + e.what();
        emscripten_console_error(g_errorMessage.c_str());
        return false;
    }
}


// 获取错误信息
std::string GetInitializationError() {
    return g_errorMessage;
}

// 绘制函数（封装Canvas方法）
void ClearCanvas(const Color& color) {
    if (g_canvas) {
        g_canvas->clear(color);
    }
}

void DrawRect(const Rect& rect, const Paint& paint) {
    if (g_canvas) {
        g_canvas->drawRect(rect, paint);
    }
}

void DrawCircle(const Point& center, float radius, const Paint& paint) {
    if (g_canvas) {
        g_canvas->drawCircle(center.x, center.y, radius, paint);
    }
}

// 呈现绘制结果
void Present() {
    if (g_window && g_context) {
        g_window->present(g_context.get());
    }
}

// 清理资源
void CleanupTGFX() {
    g_canvas = nullptr;
    g_surface = nullptr;
    g_context = nullptr;
    g_window = nullptr;
}

EMSCRIPTEN_BINDINGS(TGFXSimpleAPI) {

    // === 基础类型绑定 ===

    // Color：保留基础构造，避免多余静态方法
    class_<Color>("Color")
        .constructor<>()
        .constructor<float, float, float>()
        .constructor<float, float, float, float>();

    function("MakeColor", &MakeColor);

    // Point：按值绑定
    value_object<Point>("Point")
        .field("x", &Point::x)
        .field("y", &Point::y);
    function("MakePoint", &MakePoint);

    // Rect：按值或基础构造
    class_<Rect>("Rect")
        .constructor<>()
        .constructor<float, float, float, float>()
        .property("left", &Rect::left)
        .property("top", &Rect::top)
        .property("right", &Rect::right)
        .property("bottom", &Rect::bottom);
    function("MakeRect", &MakeRect);

    // Paint：仅保留必要的设置接口
    class_<Paint>("Paint")
        .constructor<>()
        .function("setColor", &Paint::setColor)
        .function("setStyle", &Paint::setStyle)
        .function("setStrokeWidth", &Paint::setStrokeWidth);

    enum_<PaintStyle>("PaintStyle")
        .value("Fill", PaintStyle::Fill)
        .value("Stroke", PaintStyle::Stroke);

    // === 全局函数（核心绘制） ===
    function("InitializeTGFX", &InitializeTGFX);
    function("GetInitializationError", &GetInitializationError);
    function("Present", &Present);
    function("CleanupTGFX", &CleanupTGFX);
    function("ClearCanvas", &ClearCanvas);
    function("DrawRect", &DrawRect);
    function("DrawCircle", &DrawCircle);
}
