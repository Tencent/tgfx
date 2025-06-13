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

#pragma once

#include "tgfx/core/ImageBuffer.h"
#include <media/NdkMediaCodec.h>
#include <android/hardware_buffer.h>

namespace tgfx {
    class AndroidOESBuffer  : public ImageBuffer {
    public:
        static std::shared_ptr<AndroidOESBuffer> MakeFrom(AHardwareBuffer* hardwareBuffer, YUVColorSpace colorSpace);

        ~AndroidOESBuffer() override;

        int width() const override;

        int height() const override;

        bool isAlphaOnly() const final {
            return false;
        }

    protected:
        std::shared_ptr<Texture> onMakeTexture(Context* context, bool mipmapped) const override;

    private:
        AHardwareBuffer* hardwareBuffer = nullptr;
        YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED;

        AndroidOESBuffer(AHardwareBuffer* hardwareBuffer, YUVColorSpace colorSpace);
    };
}  // namespace tgfx

#endif