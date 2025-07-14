#include "DisplayLink.h"

DisplayLink::DisplayLink(std::function<void()> callback) : callback(callback) {
    const char name[] = "hello2d_vsync";
    vSync = OH_NativeVSync_Create(name, sizeof(name) - 1);
}

DisplayLink::~DisplayLink() {
    if (vSync != nullptr) {
        OH_NativeVSync_Destroy(vSync);
    }
}

void DisplayLink::start() {
    if (vSync != nullptr) {
        OH_NativeVSync_RequestFrame(vSync, &DisplayLink::VSyncCallback, this);
    }
}

void DisplayLink::VSyncCallback(long long, void* data) {
    auto* displayLink = static_cast<DisplayLink*>(data);
    displayLink->callback();
    OH_NativeVSync_RequestFrame(displayLink->vSync, &DisplayLink::VSyncCallback, displayLink);
}
