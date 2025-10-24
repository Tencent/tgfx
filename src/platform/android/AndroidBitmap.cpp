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

#include "tgfx/platform/android/AndroidBitmap.h"
#include <android/bitmap.h>
#include "AHardwareBufferFunctions.h"
#include "JNIUtil.h"
#include "core/utils/ColorSpaceHelper.h"

namespace tgfx {
static constexpr int BITMAP_FLAGS_ALPHA_UNPREMUL = 2;
static constexpr int BITMAP_FORMAT_RGBA_F16 = 9;
static constexpr int BITMAP_FORMAT_RGBA_1010102 = 10;

static Global<jclass> BitmapClass;
static jmethodID Bitmap_getColorSpace;
static Global<jclass> ColorSpaceClass;
static jmethodID ColorSpace_getDataSpace;
static Global<jclass> DataSpaceClass;
static jmethodID DataSpaceClass_getStandard;
static jmethodID DataSpaceClass_getTransfer;

void AndroidBitmap::JNIInit(JNIEnv* env) {
  BitmapClass = env->FindClass("android/graphics/Bitmap");
  Bitmap_getColorSpace =
      env->GetMethodID(BitmapClass.get(), "getColorSpace", "()Landroid/graphics/ColorSpace;");
  if (Bitmap_getColorSpace == nullptr) {
    env->ExceptionClear();
  }
  ColorSpaceClass = env->FindClass("android/graphics/ColorSpace");
  if (ColorSpaceClass.get() != nullptr) {
    ColorSpace_getDataSpace = env->GetMethodID(ColorSpaceClass.get(), "getColorSpace", "()I");
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
  DataSpaceClass = env->FindClass("android/hardware/DataSpace");
  if (DataSpaceClass.get() != nullptr) {
    DataSpaceClass_getStandard =
        env->GetStaticMethodID(DataSpaceClass.get(), "getStandard", "(I)I");
    DataSpaceClass_getTransfer =
        env->GetStaticMethodID(DataSpaceClass.get(), "getTransfer", "(I)I");
  } else {
    env->ExceptionClear();
  }
}

ImageInfo AndroidBitmap::GetInfo(JNIEnv* env, jobject bitmap) {
  if (env == nullptr || bitmap == nullptr) {
    return {};
  }
  AndroidBitmapInfo bitmapInfo = {};
  if (AndroidBitmap_getInfo(env, bitmap, &bitmapInfo) != 0) {
    env->ExceptionClear();
    return {};
  }
  AlphaType alphaType = (bitmapInfo.flags & BITMAP_FLAGS_ALPHA_UNPREMUL)
                            ? AlphaType::Unpremultiplied
                            : AlphaType::Premultiplied;
  ColorType colorType;
  switch (bitmapInfo.format) {
    case ANDROID_BITMAP_FORMAT_RGBA_8888:
      colorType = ColorType::RGBA_8888;
      break;
    case ANDROID_BITMAP_FORMAT_A_8:
      colorType = ColorType::ALPHA_8;
      break;
    case ANDROID_BITMAP_FORMAT_RGB_565:
      colorType = ColorType::RGB_565;
      break;
    case BITMAP_FORMAT_RGBA_F16:
      colorType = ColorType::RGBA_F16;
      break;
    case BITMAP_FORMAT_RGBA_1010102:
      colorType = ColorType::RGBA_1010102;
      break;
    default:
      colorType = ColorType::Unknown;
      break;
  }
  return ImageInfo::Make(static_cast<int>(bitmapInfo.width), static_cast<int>(bitmapInfo.height),
                         colorType, alphaType, bitmapInfo.stride);
}

HardwareBufferRef AndroidBitmap::GetHardwareBuffer(JNIEnv* env, jobject bitmap) {
  static const auto fromBitmap = AHardwareBufferFunctions::Get()->fromBitmap;
  static const auto release = AHardwareBufferFunctions::Get()->release;
  if (fromBitmap == nullptr || release == nullptr || bitmap == nullptr) {
    return nullptr;
  }
  AHardwareBuffer* hardwareBuffer = nullptr;
  fromBitmap(env, bitmap, &hardwareBuffer);
  if (hardwareBuffer != nullptr) {
    // The hardware buffer returned by AndroidBitmap_getHardwareBuffer() has a reference count of 1.
    // We decrease it to keep the behaviour the same as AHardwareBuffer_fromHardwareBuffer().
    release(hardwareBuffer);
  }
  return hardwareBuffer;
}

std::shared_ptr<ColorSpace> AndroidBitmap::GetColorSpace(JNIEnv* env, jobject bitmap) {
  if (env == nullptr || bitmap == nullptr) {
    return ColorSpace::MakeSRGB();
  }
  if (Bitmap_getColorSpace) {
    jobject colorSpaceObj = env->CallObjectMethod(bitmap, Bitmap_getColorSpace);
    if (ColorSpace_getDataSpace) {
      jint dataSpace = env->CallIntMethod(colorSpaceObj, ColorSpace_getDataSpace);
      jint standard =
          env->CallStaticIntMethod(DataSpaceClass.get(), DataSpaceClass_getStandard, dataSpace);
      jint transfer =
          env->CallStaticIntMethod(DataSpaceClass.get(), DataSpaceClass_getTransfer, dataSpace);
      return AndroidDataSpaceToColorSpace(standard, transfer);
    }
  }
  return ColorSpace::MakeSRGB();
}
}  // namespace tgfx