#pragma once
#include <functional>
#include <native_vsync/native_vsync.h>

class DisplayLink {
public:
    explicit DisplayLink(std::function<void()> callback);
    ~DisplayLink();
    void start();
    void pause();

private:
    static void VSyncCallback(long long, void* data);
    OH_NativeVSync* vSync = nullptr;
    std::function<void()> callback = nullptr;
    bool isPaused = false;
};