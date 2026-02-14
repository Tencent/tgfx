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
static std::mutex deviceLocker = {};
static std::vector<EAGLDevice*> deviceList = {};
static std::vector<EAGLDevice*> delayPurgeList = {};
static std::atomic_bool appInBackground = {true};

void ApplicationWillResignActive() {
  // Set applicationInBackground to true first to ensure that no new GL operation is generated
  // during the callback process.
  appInBackground = true;
  std::vector<std::shared_ptr<EAGLDevice>> devices = {};
  {
    std::lock_guard<std::mutex> autoLock(deviceLocker);
    devices.reserve(deviceList.size());
    for (auto& device : deviceList) {
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

void ApplicationDidBecomeActive() {
  appInBackground = false;
  std::vector<EAGLDevice*> delayList = {};
  deviceLocker.lock();
  std::swap(delayList, delayPurgeList);
  deviceLocker.unlock();
  for (auto& device : delayList) {
    delete device;
  }
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
  std::lock_guard<std::mutex> autoLock(deviceLocker);
  delayPurgeList.push_back(device);
}

EAGLDevice::EAGLDevice(std::unique_ptr<GPU> gpu, EAGLContext* eaglContext)
    : GLDevice(std::move(gpu), eaglContext), _eaglContext(eaglContext) {
  [_eaglContext retain];
  std::lock_guard<std::mutex> autoLock(deviceLocker);
  auto index = deviceList.size();
  deviceList.push_back(this);
  cacheArrayIndex = index;
}

EAGLDevice::~EAGLDevice() {
  {
    std::lock_guard<std::mutex> autoLock(deviceLocker);
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
+ (void)applicationWillResignActive:(NSNotification*)notification {
  tgfx::ApplicationWillResignActive();
}

+ (void)applicationDidBecomeActive:(NSNotification*)notification {
  tgfx::ApplicationDidBecomeActive();
}
@end

static bool RegisterNotifications() {
  [[NSNotificationCenter defaultCenter] addObserver:[TGFXAppMonitor class]
                                           selector:@selector(applicationWillResignActive:)
                                               name:UIApplicationWillResignActiveNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:[TGFXAppMonitor class]
                                           selector:@selector(applicationDidBecomeActive:)
                                               name:UIApplicationDidBecomeActiveNotification
                                             object:nil];
  return true;
}

static bool registered = RegisterNotifications();
