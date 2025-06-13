/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#if __ANDROID_API__ >= 26

#include "AndroidOESBuffer.h"
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <mutex>
#include <unordered_map>
#include <tgfx/gpu/opengl/GLTypes.h>
#include "gpu/Texture.h"
#include "tgfx/gpu/opengl/GLFunctions.h"
#include "core/utils/WeakMap.h"

namespace tgfx {
    static WeakMap<AHardwareBuffer*, AndroidOESBuffer> oesBufferMap = {};
    std::shared_ptr<AndroidOESBuffer> AndroidOESBuffer::MakeFrom(AHardwareBuffer* hardwareBuffer, YUVColorSpace colorSpace) {
        if (!hardwareBuffer) {
            return nullptr;
        }
        if(auto cached = oesBufferMap.find(hardwareBuffer)) {
            return cached;
        }
        auto buffer = std::shared_ptr<AndroidOESBuffer>(new AndroidOESBuffer(hardwareBuffer, colorSpace));
        oesBufferMap.insert(hardwareBuffer, buffer);
        return buffer;
    }

    AndroidOESBuffer::AndroidOESBuffer(AHardwareBuffer* hardwareBuffer, YUVColorSpace colorSpace)
            : hardwareBuffer(hardwareBuffer), colorSpace(colorSpace) {
        AHardwareBuffer_acquire(hardwareBuffer);
    }

    AndroidOESBuffer::~AndroidOESBuffer() {
        if (hardwareBuffer) {
            oesBufferMap.remove(hardwareBuffer);
            AHardwareBuffer_release(hardwareBuffer);
        }
    }

    int AndroidOESBuffer::width() const {
        if (!hardwareBuffer) return 0;
        AHardwareBuffer_Desc desc{};
        AHardwareBuffer_describe(hardwareBuffer, &desc);
        return static_cast<int>(desc.width);
    }

    int AndroidOESBuffer::height() const {
        if (!hardwareBuffer) return 0;
        AHardwareBuffer_Desc desc{};
        AHardwareBuffer_describe(hardwareBuffer, &desc);
        return static_cast<int>(desc.height);
    }

    std::shared_ptr<Texture> AndroidOESBuffer::onMakeTexture(Context* context, bool /*mipmapped*/) const {

        return Texture::MakeFrom(context, hardwareBuffer, colorSpace);
    }

} // tgfx

#endif
