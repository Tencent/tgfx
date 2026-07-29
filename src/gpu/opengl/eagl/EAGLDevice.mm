/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/gpu/opengl/eagl/EAGLDevice.h"
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES3/glext.h>
#include "gpu/opengl/eagl/EAGLGPU.h"

namespace tgfx {
static std::atomic_bool appInBackground = {true};

struct DeviceRegistry {
  std::mutex deviceLocker = {};
  std::vector<EAGLDevice*> deviceList = {};
  std::vector<EAGLDevice*> delayPurgeList = {};
};

static DeviceRegistry& GetRegistry() {
  static auto* registry = new DeviceRegistry();
  return *registry;
}

void ApplicationDidEnterBackground() {
  // The application has actually transitioned to the background. Per Apple's documentation,
  // executing OpenGL ES commands from this point on will cause iOS to terminate the process,
  // so:
  //   1) Close the gate first to make sure no new GL commands can be produced from this point on.
  //   2) Then call glFinish() on every active device to flush any commands that were already in
  //      flight when the gate was closed, so that they complete on the GPU before the app is
  //      actually suspended.
  //
  // These two steps must happen together at exactly the same lifecycle event. If the gate were
  // closed earlier (e.g. on applicationWillResignActive:) but glFinish() were performed there as
  // well, new GL commands generated between the flush and the actual transition into background
  // (e.g. by CADisplayLink callbacks during the inactive period) could still slip through and be
  // executed by the GPU after the app is suspended, which would terminate the process.
  appInBackground = true;
  std::vector<std::shared_ptr<EAGLDevice>> devices = {};
  {
    auto& registry = GetRegistry();
    std::lock_guard<std::mutex> autoLock(registry.deviceLocker);
    devices.reserve(registry.deviceList.size());
    for (auto& device : registry.deviceList) {
      auto shared = std::static_pointer_cast<EAGLDevice>(device->weakThis.lock());
      if (shared) {
        devices.push_back(std::move(shared));
      }
    }
  }
  for (auto& device : devices) {
    device->finish();
  }
}

void ApplicationWillEnterForeground() {
  // The application is about to return to the foreground. OpenGL ES calls are legal again from
  // this point on, so:
  //   1) Drain delayPurgeList first, while the gate is still closed. EAGLDevice instances whose
  //      reference count reached zero during the background period were parked on this list
  //      because deleting them would have issued GL commands (e.g. glDeleteTextures from
  //      releaseAll()) at a moment when GL was forbidden. Now is the right time to actually
  //      delete them.
  //   2) Open the gate only after the purge has completed.
  //
  // The ordering matters: if the gate were opened first, render threads could observe
  // appInBackground == false, acquire an EAGLDevice via GLDevice::Current()/Make(), and start
  // issuing GL commands on this thread's context while we are concurrently destroying other
  // EAGLDevices on the same sharegroup (each ~EAGLDevice() switches the current context to run
  // releaseAll()). Draining first guarantees that no GL deletion races with any new rendering
  // work scheduled after the gate opens.
  std::vector<EAGLDevice*> delayList = {};
  {
    auto& registry = GetRegistry();
    std::lock_guard<std::mutex> autoLock(registry.deviceLocker);
    std::swap(delayList, registry.delayPurgeList);
  }
  for (auto& device : delayList) {
    delete device;
  }
  appInBackground = false;
}

void ApplicationDidBecomeActive() {
  // Cold-launch is the only lifecycle path where applicationWillEnterForeground: is not
  // delivered (the system posts didBecomeActive: directly), so this is the canonical fallback
  // for opening the gate. delayPurgeList is necessarily empty on the cold-launch path because
  // no EAGLDevice could have been registered, then released, before the process had even
  // started, so no purge step is needed here.
  appInBackground = false;
}

void* GLDevice::CurrentNativeHandle() {
  return [EAGLContext currentContext];
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  if (appInBackground) {
    return nullptr;
  }
  return EAGLDevice::Wrap([EAGLContext currentContext], true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  if (appInBackground) {
    return nullptr;
  }
  EAGLContext* eaglContext = nil;
  EAGLContext* eaglShareContext = reinterpret_cast<EAGLContext*>(sharedContext);
  if (eaglShareContext != nil) {
    eaglContext = [[EAGLContext alloc] initWithAPI:[eaglShareContext API]
                                        sharegroup:[eaglShareContext sharegroup]];
  } else {
    eaglContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (eaglContext == nil) {
      eaglContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    }
  }
  if (eaglContext == nil) {
    return nullptr;
  }
  auto device = EAGLDevice::Wrap(eaglContext, false);
  [eaglContext release];
  return device;
}

std::shared_ptr<EAGLDevice> EAGLDevice::MakeFrom(EAGLContext* eaglContext) {
  if (appInBackground) {
    return nullptr;
  }
  return EAGLDevice::Wrap(eaglContext, true);
}

std::shared_ptr<EAGLDevice> EAGLDevice::Wrap(EAGLContext* eaglContext, bool externallyOwned) {
  if (eaglContext == nil) {
    return nullptr;
  }
  auto glDevice = GLDevice::Get(eaglContext);
  if (glDevice) {
    return std::static_pointer_cast<EAGLDevice>(glDevice);
  }
  auto oldEAGLContext = [[EAGLContext currentContext] retain];
  if (oldEAGLContext != eaglContext) {
    auto result = [EAGLContext setCurrentContext:eaglContext];
    if (!result) {
      [oldEAGLContext release];
      return nullptr;
    }
  }
  std::shared_ptr<EAGLDevice> device = nullptr;
  auto interface = GLInterface::GetNative();
  if (interface != nullptr) {
    auto gpu = std::make_unique<EAGLGPU>(std::move(interface), eaglContext);
    device = std::shared_ptr<EAGLDevice>(new EAGLDevice(std::move(gpu), eaglContext),
                                         EAGLDevice::NotifyReferenceReachedZero);
    device->externallyOwned = externallyOwned;
    device->weakThis = device;
  }
  if (oldEAGLContext != eaglContext) {
    [EAGLContext setCurrentContext:oldEAGLContext];
  }
  [oldEAGLContext release];
  return device;
}

void EAGLDevice::NotifyReferenceReachedZero(EAGLDevice* device) {
  if (!appInBackground) {
    delete device;
    return;
  }
  auto& registry = GetRegistry();
  std::lock_guard<std::mutex> autoLock(registry.deviceLocker);
  registry.delayPurgeList.push_back(device);
}

EAGLDevice::EAGLDevice(std::unique_ptr<GPU> gpu, EAGLContext* eaglContext)
    : GLDevice(std::move(gpu), eaglContext), _eaglContext(eaglContext) {
  [_eaglContext retain];
  auto& registry = GetRegistry();
  std::lock_guard<std::mutex> autoLock(registry.deviceLocker);
  auto index = registry.deviceList.size();
  registry.deviceList.push_back(this);
  cacheArrayIndex = index;
}

EAGLDevice::~EAGLDevice() {
  {
    auto& registry = GetRegistry();
    std::lock_guard<std::mutex> autoLock(registry.deviceLocker);
    auto& deviceList = registry.deviceList;
    auto tail = *(deviceList.end() - 1);
    auto index = cacheArrayIndex;
    deviceList[index] = tail;
    tail->cacheArrayIndex = index;
    deviceList.pop_back();
  }
  releaseAll();
  [_eaglContext release];
}

bool EAGLDevice::sharableWith(void* nativeContext) const {
  if (nativeContext == nullptr) {
    return false;
  }
  auto shareContext = static_cast<EAGLContext*>(nativeContext);
  return [_eaglContext sharegroup] == [shareContext sharegroup];
}

EAGLContext* EAGLDevice::eaglContext() const {
  return _eaglContext;
}

bool EAGLDevice::onLockContext() {
  return makeCurrent();
}

void EAGLDevice::onUnlockContext() {
  clearCurrent();
}

bool EAGLDevice::makeCurrent(bool force) {
  if (!force && appInBackground) {
    return false;
  }
  oldContext = [[EAGLContext currentContext] retain];
  if (oldContext == _eaglContext) {
    return true;
  }
  if (![EAGLContext setCurrentContext:_eaglContext]) {
    [oldContext release];
    oldContext = nil;
    return false;
  }
  return true;
}

void EAGLDevice::clearCurrent() {
  if (oldContext == _eaglContext) {
    [oldContext release];
    oldContext = nil;
    return;
  }
  [EAGLContext setCurrentContext:nil];
  if (oldContext) {
    // 可能失败。
    [EAGLContext setCurrentContext:oldContext];
  }
  [oldContext release];
  oldContext = nil;
}

void EAGLDevice::finish() {
  std::lock_guard<std::mutex> autoLock(locker);
  if (makeCurrent(true)) {
    glFinish();
    clearCurrent();
  }
}
}  // namespace tgfx

@interface TGFXAppMonitor : NSObject
@end

@implementation TGFXAppMonitor
+ (void)applicationDidEnterBackground:(NSNotification*)notification {
  tgfx::ApplicationDidEnterBackground();
}

+ (void)applicationWillEnterForeground:(NSNotification*)notification {
  tgfx::ApplicationWillEnterForeground();
}

+ (void)applicationDidBecomeActive:(NSNotification*)notification {
  tgfx::ApplicationDidBecomeActive();
}
@end

static bool RegisterNotifications() {
  [[NSNotificationCenter defaultCenter] addObserver:[TGFXAppMonitor class]
                                           selector:@selector(applicationDidEnterBackground:)
                                               name:UIApplicationDidEnterBackgroundNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:[TGFXAppMonitor class]
                                           selector:@selector(applicationWillEnterForeground:)
                                               name:UIApplicationWillEnterForegroundNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:[TGFXAppMonitor class]
                                           selector:@selector(applicationDidBecomeActive:)
                                               name:UIApplicationDidBecomeActiveNotification
                                             object:nil];
  return true;
}

static bool registered = RegisterNotifications();
