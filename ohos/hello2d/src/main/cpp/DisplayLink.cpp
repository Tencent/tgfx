#include "DisplayLink.h"

DisplayLink::DisplayLink(std::function<void()> callback) : callback(callback) {
    const char name[] = "hello2d_vsync";
    vSync = OH_NativeVSync_Create(name, strlen(name));
}

DisplayLink::~DisplayLink() {
    if (vSync != nullptr) {
        OH_NativeVSync_Destroy(vSync);
    }
}

void DisplayLink::start() {
    if (vSync != nullptr) {
        OH_NativeVSync_RequestFrame(vSync, &DisplayLink::VSyncCallback, this);
        isStopped = false;
    }
}

void DisplayLink::stop() {
    if (vSync != nullptr && !isStopped) {
        isStopped = true;
    }
}

void DisplayLink::VSyncCallback(long long, void* data) {
    auto* displayLink = static_cast<DisplayLink*>(data);
    if (!displayLink->isStopped) {
        displayLink->callback();
        OH_NativeVSync_RequestFrame(displayLink->vSync, &DisplayLink::VSyncCallback, displayLink);
    }
}
