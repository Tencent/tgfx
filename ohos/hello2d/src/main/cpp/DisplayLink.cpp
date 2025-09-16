#include "DisplayLink.h"

DisplayLink::DisplayLink(std::function<void()> callback) : callback(callback) {
    const char name[] = "hello2d_vsync";
    vSync = OH_NativeVSync_Create(name, strlen(name));
}

DisplayLink::~DisplayLink() {
    OH_NativeVSync_Destroy(vSync);
}

void DisplayLink::start() {
    if (playing == false) {
        OH_NativeVSync_RequestFrame(vSync, &DisplayLink::VSyncCallback, this);
        playing = true;
    }
}

void DisplayLink::stop() {
    playing = false;
}

void DisplayLink::VSyncCallback(long long, void* data) {
    auto displayLink = static_cast<DisplayLink*>(data);
    if (displayLink->playing) {
        displayLink->callback();
        OH_NativeVSync_RequestFrame(displayLink->vSync, &DisplayLink::VSyncCallback, displayLink);
    }
}
