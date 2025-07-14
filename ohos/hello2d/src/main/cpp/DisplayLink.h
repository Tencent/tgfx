#pragma once
#include <functional>
#include <native_vsync/native_vsync.h>

class DisplayLink {
public:
    explicit DisplayLink(std::function<void()> callback);
    ~DisplayLink();
    void start();

private:
    static void VSyncCallback(long long, void* data);
    OH_NativeVSync* vSync = nullptr;
    std::function<void()> callback;
};