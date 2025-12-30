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

#include "NativeCodec.h"
#include <android/bitmap.h>
#include <cstdio>
#include "NativeImageBuffer.h"
#include "core/utils/Log.h"
#include "platform/android/AHardwareBufferFunctions.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/platform/android/AndroidBitmap.h"

namespace tgfx {
static Global<jclass> ColorSpaceClass;
static jmethodID ColorSpace_get;
static Global<jclass> ColorSpaceNamedClass;
static jfieldID ColorSpaceNamed_SRGB;
static Global<jclass> BitmapFactoryOptionsClass;
static jmethodID BitmapFactoryOptions_Constructor;
static jfieldID BitmapFactoryOptions_inJustDecodeBounds;
static jfieldID BitmapFactoryOptions_inPreferredConfig;
static jfieldID BitmapFactoryOptions_inPremultiplied;
static jfieldID BitmapFactoryOptions_outWidth;
static jfieldID BitmapFactoryOptions_outHeight;
static jfieldID BitmapFactoryOptions_inPreferredColorSpace;
static Global<jclass> BitmapFactoryClass;
static jmethodID BitmapFactory_decodeFile;
static jmethodID BitmapFactory_decodeByteArray;
static Global<jclass> ByteArrayInputStreamClass;
static jmethodID ByteArrayInputStream_Constructor;
static Global<jclass> ExifInterfaceClass;
static jmethodID ExifInterface_Constructor_Path;
static jmethodID ExifInterface_Constructor_Stream;
static jmethodID ExifInterfaceClass_getAttributeInt;
static Global<jclass> BitmapClass;
static jmethodID Bitmap_copy;
static jmethodID Bitmap_getConfig;
static jmethodID Bitmap_createBitmap;
static Global<jclass> BitmapConfigClass;
static jmethodID BitmapConfig_equals;
static jfieldID BitmapConfig_ALPHA_8;
static jfieldID BitmapConfig_ARGB_8888;
static jfieldID BitmapConfig_RGB_565;
static jfieldID BitmapConfig_HARDWARE;

void NativeCodec::JNIInit(JNIEnv* env) {
  BitmapFactoryOptionsClass = env->FindClass("android/graphics/BitmapFactory$Options");
  if (BitmapFactoryOptionsClass.get() == nullptr) {
    LOGE("Could not run NativeCodec.InitJNI(), BitmapFactoryOptionsClass is not found!");
    return;
  }
  BitmapFactoryOptions_Constructor =
      env->GetMethodID(BitmapFactoryOptionsClass.get(), "<init>", "()V");
  BitmapFactoryOptions_inJustDecodeBounds =
      env->GetFieldID(BitmapFactoryOptionsClass.get(), "inJustDecodeBounds", "Z");
  BitmapFactoryOptions_inPreferredConfig = env->GetFieldID(
      BitmapFactoryOptionsClass.get(), "inPreferredConfig", "Landroid/graphics/Bitmap$Config;");
  BitmapFactoryOptions_inPremultiplied =
      env->GetFieldID(BitmapFactoryOptionsClass.get(), "inPremultiplied", "Z");
  BitmapFactoryOptions_outWidth = env->GetFieldID(BitmapFactoryOptionsClass.get(), "outWidth", "I");
  BitmapFactoryOptions_outHeight =
      env->GetFieldID(BitmapFactoryOptionsClass.get(), "outHeight", "I");
  // for color space conversion (API level 26+)
  ColorSpaceClass = env->FindClass("android/graphics/ColorSpace");
  if (ColorSpaceClass.get() != nullptr) {
    ColorSpace_get = env->GetStaticMethodID(
        ColorSpaceClass.get(), "get",
        "(Landroid/graphics/ColorSpace$Named;)Landroid/graphics/ColorSpace;");
    ColorSpaceNamedClass = env->FindClass("android/graphics/ColorSpace$Named");
    ColorSpaceNamed_SRGB = env->GetStaticFieldID(ColorSpaceNamedClass.get(), "SRGB",
                                                 "Landroid/graphics/ColorSpace$Named;");
    BitmapFactoryOptions_inPreferredColorSpace = env->GetFieldID(
        BitmapFactoryOptionsClass.get(), "inPreferredColorSpace", "Landroid/graphics/ColorSpace;");
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    BitmapFactoryOptions_inPreferredColorSpace = nullptr;
  }
  ByteArrayInputStreamClass = env->FindClass("java/io/ByteArrayInputStream");
  ByteArrayInputStream_Constructor =
      env->GetMethodID(ByteArrayInputStreamClass.get(), "<init>", "([B)V");
  ExifInterfaceClass = env->FindClass("androidx/exifinterface/media/ExifInterface");
  if (ExifInterfaceClass.get() != nullptr) {
    ExifInterface_Constructor_Path =
        env->GetMethodID(ExifInterfaceClass.get(), "<init>", "(Ljava/lang/String;)V");
    ExifInterface_Constructor_Stream =
        env->GetMethodID(ExifInterfaceClass.get(), "<init>", "(Ljava/io/InputStream;)V");
    ExifInterfaceClass_getAttributeInt =
        env->GetMethodID(ExifInterfaceClass.get(), "getAttributeInt", "(Ljava/lang/String;I)I");
  } else {
    env->ExceptionClear();
    LOGE(
        "NativeCodec.InitJNI(): \"androidx/exifinterface/media/ExifInterface\" is not found! We "
        "may be unable to determine image origins.");
  }
  BitmapFactoryClass = env->FindClass("android/graphics/BitmapFactory");
  BitmapFactory_decodeFile = env->GetStaticMethodID(
      BitmapFactoryClass.get(), "decodeFile",
      "(Ljava/lang/String;Landroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;");
  BitmapFactory_decodeByteArray = env->GetStaticMethodID(
      BitmapFactoryClass.get(), "decodeByteArray",
      "([BIILandroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;");
  BitmapClass = env->FindClass("android/graphics/Bitmap");
  Bitmap_copy = env->GetMethodID(BitmapClass.get(), "copy",
                                 "(Landroid/graphics/Bitmap$Config;Z)Landroid/graphics/Bitmap;");
  Bitmap_getConfig =
      env->GetMethodID(BitmapClass.get(), "getConfig", "()Landroid/graphics/Bitmap$Config;");
  Bitmap_createBitmap =
      env->GetStaticMethodID(BitmapClass.get(), "createBitmap",
                             "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
  BitmapConfigClass = env->FindClass("android/graphics/Bitmap$Config");
  BitmapConfig_equals =
      env->GetMethodID(BitmapConfigClass.get(), "equals", "(Ljava/lang/Object;)Z");
  BitmapConfig_ALPHA_8 =
      env->GetStaticFieldID(BitmapConfigClass.get(), "ALPHA_8", "Landroid/graphics/Bitmap$Config;");
  BitmapConfig_ARGB_8888 = env->GetStaticFieldID(BitmapConfigClass.get(), "ARGB_8888",
                                                 "Landroid/graphics/Bitmap$Config;");
  BitmapConfig_RGB_565 =
      env->GetStaticFieldID(BitmapConfigClass.get(), "RGB_565", "Landroid/graphics/Bitmap$Config;");
  BitmapConfig_HARDWARE = env->GetStaticFieldID(BitmapConfigClass.get(), "HARDWARE",
                                                "Landroid/graphics/Bitmap$Config;");
  // BitmapConfig_HARDWARE may be nullptr.
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
}

std::shared_ptr<NativeCodec> NativeCodec::Make(JNIEnv* env, jobject sizeObject, int orientation) {
  auto size = env->GetIntArrayElements(static_cast<jintArray>(sizeObject), nullptr);
  int width = size[0];
  int height = size[1];
  env->ReleaseIntArrayElements(static_cast<jintArray>(sizeObject), size, 0);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::shared_ptr<NativeCodec>(
      new NativeCodec(width, height, static_cast<Orientation>(orientation)));
}

static Orientation GetOrientation(JNIEnv* env, jobject exifInterface) {
  if (exifInterface == nullptr) {
    env->ExceptionClear();
    return Orientation::TopLeft;
  }
  auto key = env->NewStringUTF("Orientation");
  auto orientation = env->CallIntMethod(exifInterface, ExifInterfaceClass_getAttributeInt, key,
                                        static_cast<int>(Orientation::TopLeft));
  return static_cast<Orientation>(orientation);
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(const std::string& filePath) {
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr || filePath.empty()) {
    return nullptr;
  }
  if (BitmapFactoryOptionsClass.get() == nullptr) {
    LOGE("Could not run NativeCodec.GetOrientation(), BitmapFactoryOptionsClass is not found!");
    return nullptr;
  }
  auto options = env->NewObject(BitmapFactoryOptionsClass.get(), BitmapFactoryOptions_Constructor);
  if (options == nullptr) {
    return nullptr;
  }
  env->SetBooleanField(options, BitmapFactoryOptions_inJustDecodeBounds, true);
  auto imagePath = SafeToJString(env, filePath);
  auto bitmap = env->CallStaticObjectMethod(BitmapFactoryClass.get(), BitmapFactory_decodeFile,
                                            imagePath, options);
  auto colorSpace = AndroidBitmap::GetColorSpace(env, bitmap);
  if (env->ExceptionCheck()) {
    return nullptr;
  }
  auto width = env->GetIntField(options, BitmapFactoryOptions_outWidth);
  auto height = env->GetIntField(options, BitmapFactoryOptions_outHeight);
  if (width <= 0 || height <= 0) {
    env->ExceptionClear();
    LOGE("NativeCodec::readPixels(): Failed to get the size of the image!");
    return nullptr;
  }
  jobject exifInterface = nullptr;
  if (ExifInterfaceClass.get() != nullptr) {
    exifInterface =
        env->NewObject(ExifInterfaceClass.get(), ExifInterface_Constructor_Path, imagePath);
  }
  auto origin = GetOrientation(env, exifInterface);
  auto codec =
      std::shared_ptr<NativeCodec>(new NativeCodec(width, height, origin, std::move(colorSpace)));
  codec->imagePath = filePath;
  return codec;
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(std::shared_ptr<Data> imageBytes) {
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr || imageBytes == nullptr) {
    return nullptr;
  }
  if (BitmapFactoryOptionsClass.get() == nullptr) {
    LOGE("Could not run NativeCodec.MakeNativeCodec(), BitmapFactoryOptionsClass is not found!");
    return nullptr;
  }
  auto options = env->NewObject(BitmapFactoryOptionsClass.get(), BitmapFactoryOptions_Constructor);
  if (options == nullptr) {
    return nullptr;
  }
  env->SetBooleanField(options, BitmapFactoryOptions_inJustDecodeBounds, true);
  auto byteSize = static_cast<jsize>(imageBytes->size());
  auto byteArray = env->NewByteArray(byteSize);
  env->SetByteArrayRegion(byteArray, 0, byteSize,
                          reinterpret_cast<const jbyte*>(imageBytes->data()));
  auto bitmap = env->CallStaticObjectMethod(BitmapFactoryClass.get(), BitmapFactory_decodeByteArray,
                                            byteArray, 0, byteSize, options);
  auto colorSpace = AndroidBitmap::GetColorSpace(env, bitmap);
  if (env->ExceptionCheck()) {
    return nullptr;
  }
  auto width = env->GetIntField(options, BitmapFactoryOptions_outWidth);
  auto height = env->GetIntField(options, BitmapFactoryOptions_outHeight);
  if (width <= 0 || height <= 0) {
    env->ExceptionClear();
    LOGE("NativeCodec::readPixels(): Failed to get the size of the image!");
    return nullptr;
  }
  auto inputStream =
      env->NewObject(ByteArrayInputStreamClass.get(), ByteArrayInputStream_Constructor, byteArray);
  jobject exifInterface = nullptr;
  if (ExifInterfaceClass.get() != nullptr) {
    exifInterface =
        env->NewObject(ExifInterfaceClass.get(), ExifInterface_Constructor_Stream, inputStream);
  }
  auto origin = GetOrientation(env, exifInterface);
  auto codec =
      std::shared_ptr<NativeCodec>(new NativeCodec(width, height, origin, std::move(colorSpace)));
  codec->imageBytes = imageBytes;
  return codec;
}

std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(NativeImageRef nativeImage) {
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return nullptr;
  }
  if (BitmapFactoryOptionsClass.get() == nullptr) {
    LOGE("Could not run NativeCodec.MakeNativeCodec(), BitmapFactoryOptionsClass is not found!");
    return nullptr;
  }
  auto info = AndroidBitmap::GetInfo(env, nativeImage);
  if (info.isEmpty()) {
    return nullptr;
  }
  auto colorSpace = AndroidBitmap::GetColorSpace(env, nativeImage);
  auto image = std::shared_ptr<NativeCodec>(
      new NativeCodec(info.width(), info.height(), Orientation::TopLeft, std::move(colorSpace)));
  image->nativeImage = nativeImage;
  return image;
}

static jobject ConvertHardwareBitmap(JNIEnv* env, jobject bitmap) {
  // The AndroidBitmapInfo does not contain the ANDROID_BITMAP_FLAGS_IS_HARDWARE flag in the old
  // versions of Android NDK, even when the Java Bitmap has the hardware config. So we check it here
  // by the Java-side methods.
  if (bitmap == nullptr) {
    return nullptr;
  }
  if (BitmapConfig_HARDWARE == nullptr) {
    return bitmap;
  }
  auto config = env->CallObjectMethod(bitmap, Bitmap_getConfig);
  if (config == nullptr) {
    return bitmap;
  }
  static Global<jobject> HardwareConfig =
      env->GetStaticObjectField(BitmapConfigClass.get(), BitmapConfig_HARDWARE);
  if (HardwareConfig.isEmpty()) {
    return bitmap;
  }
  auto result = env->CallBooleanMethod(config, BitmapConfig_equals, HardwareConfig.get());
  if (result) {
    static Global<jobject> RGBA_Config =
        env->GetStaticObjectField(BitmapConfigClass.get(), BitmapConfig_ARGB_8888);
    auto newBitmap = env->CallObjectMethod(bitmap, Bitmap_copy, RGBA_Config.get(), JNI_FALSE);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return bitmap;
    }
    bitmap = newBitmap;
  }
  return bitmap;
}

bool NativeCodec::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                               std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const {
  if (dstPixels == nullptr) {
    return false;
  }
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return false;
  }
  auto bitmap = decodeBitmap(env, colorType, alphaType, false);
  auto info = AndroidBitmap::GetInfo(env, bitmap);
  if (info.isEmpty()) {
    LOGE("NativeCodec::readPixels() Failed to read the image info from a Bitmap!");
    return false;
  }
  void* pixels = nullptr;
  if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != 0) {
    env->ExceptionClear();
    LOGE("NativeCodec::readPixels() Failed to lockPixels() of a Java Bitmap!");
    return false;
  }
  auto dstInfo =
      ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes, dstColorSpace);
  auto result = Pixmap(info, pixels).readPixels(dstInfo, dstPixels);
  AndroidBitmap_unlockPixels(env, bitmap);
  return result;
}

std::shared_ptr<ImageBuffer> NativeCodec::onMakeBuffer(bool tryHardware) const {
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return nullptr;
  }
  auto bitmap = decodeBitmap(env, ColorType::RGBA_8888, AlphaType::Premultiplied, tryHardware);
  auto colorSpace = AndroidBitmap::GetColorSpace(env, bitmap);
  if (tryHardware) {
    auto hardwareBuffer = AndroidBitmap::GetHardwareBuffer(env, nativeImage.get());
    auto imageBuffer = PixelBuffer::MakeFrom(hardwareBuffer, colorSpace);
    if (imageBuffer != nullptr) {
      return imageBuffer;
    }
    bitmap = ConvertHardwareBitmap(env, bitmap);
  }
  auto imageBuffer = NativeImageBuffer::MakeFrom(env, bitmap);
  if (imageBuffer != nullptr) {
    return imageBuffer;
  }
  return ImageCodec::onMakeBuffer(tryHardware);
}

jobject NativeCodec::decodeBitmap(JNIEnv* env, ColorType colorType, AlphaType alphaType,
                                  bool tryHardware) const {
  if (!nativeImage.isEmpty()) {
    if (!tryHardware) {
      return ConvertHardwareBitmap(env, nativeImage.get());
    }
    return nativeImage.get();
  }
  auto options = env->NewObject(BitmapFactoryOptionsClass.get(), BitmapFactoryOptions_Constructor);
  if (options == nullptr) {
    env->ExceptionClear();
    LOGE("NativeCodec::decodeBitmap() Failed to create a BitmapFactory.Options object!");
    return nullptr;
  }
  jobject config;
  static const bool HasHardwareBitmapSupport =
      AHardwareBufferFunctions::Get()->fromBitmap != nullptr;
  if (tryHardware && HasHardwareBitmapSupport) {
    config = env->GetStaticObjectField(BitmapConfigClass.get(), BitmapConfig_HARDWARE);
  } else if (colorType == ColorType::ALPHA_8) {
    config = env->GetStaticObjectField(BitmapConfigClass.get(), BitmapConfig_ALPHA_8);
  } else if (colorType == ColorType::RGB_565) {
    config = env->GetStaticObjectField(BitmapConfigClass.get(), BitmapConfig_RGB_565);
  } else {
    config = env->GetStaticObjectField(BitmapConfigClass.get(), BitmapConfig_ARGB_8888);
  }
  env->SetObjectField(options, BitmapFactoryOptions_inPreferredConfig, config);
  if (alphaType == AlphaType::Unpremultiplied) {
    env->SetBooleanField(options, BitmapFactoryOptions_inPremultiplied, false);
  }

  if (BitmapFactoryOptions_inPreferredColorSpace != nullptr) {
    auto sRGBObj = env->GetStaticObjectField(ColorSpaceNamedClass.get(), ColorSpaceNamed_SRGB);
    auto colorSpaceObject =
        env->CallStaticObjectMethod(ColorSpaceClass.get(), ColorSpace_get, sRGBObj);
    env->SetObjectField(options, BitmapFactoryOptions_inPreferredColorSpace, colorSpaceObject);
  }

  if (!imagePath.empty()) {
    auto filePath = SafeToJString(env, imagePath);
    auto bitmap = env->CallStaticObjectMethod(BitmapFactoryClass.get(), BitmapFactory_decodeFile,
                                              filePath, options);
    if (env->ExceptionCheck()) {
      LOGE("NativeCodec::decodeBitmap() Failed to decode a Bitmap from the path: %s!",
           imagePath.c_str());
      return nullptr;
    }
    return bitmap;
  }
  auto byteSize = static_cast<jsize>(imageBytes->size());
  auto byteArray = env->NewByteArray(byteSize);
  env->SetByteArrayRegion(byteArray, 0, byteSize,
                          reinterpret_cast<const jbyte*>(imageBytes->data()));
  auto bitmap = env->CallStaticObjectMethod(BitmapFactoryClass.get(), BitmapFactory_decodeByteArray,
                                            byteArray, 0, byteSize, options);
  if (env->ExceptionCheck()) {
    LOGE("NativeCodec::decodeBitmap() Failed to decode a Bitmap from the image bytes!");
    return nullptr;
  }
  return bitmap;
}

std::shared_ptr<Data> ImageCodec::EncodeHEICWithNativeCodec(const Pixmap& pixmap, int quality) {
  if (pixmap.isEmpty()) {
    return nullptr;
  }
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return nullptr;
  }

  // Try to use AndroidX HeifWriter first (requires app to add androidx.heifwriter dependency)
  auto HeifWriterBuilderClass = env->FindClass("androidx/heifwriter/HeifWriter$Builder");
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    HeifWriterBuilderClass = nullptr;
  }

  if (HeifWriterBuilderClass == nullptr) {
    LOGI("HeifWriter not available, HEIC encoding not supported on Android");
    return nullptr;
  }

  auto width = pixmap.info().width();
  auto height = pixmap.info().height();

  // Create temp file for output
  auto FileClass = env->FindClass("java/io/File");
  auto File_createTempFile = env->GetStaticMethodID(
      FileClass, "createTempFile", "(Ljava/lang/String;Ljava/lang/String;)Ljava/io/File;");
  auto File_getAbsolutePath =
      env->GetMethodID(FileClass, "getAbsolutePath", "()Ljava/lang/String;");
  auto File_delete = env->GetMethodID(FileClass, "delete", "()Z");

  auto prefixStr = env->NewStringUTF("heic_output");
  auto suffixStr = env->NewStringUTF(".heic");
  auto tempFile = env->CallStaticObjectMethod(FileClass, File_createTempFile, prefixStr, suffixStr);
  if (tempFile == nullptr || env->ExceptionCheck()) {
    env->ExceptionClear();
    return nullptr;
  }
  auto tempFilePathJStr =
      static_cast<jstring>(env->CallObjectMethod(tempFile, File_getAbsolutePath));
  const char* pathChars = env->GetStringUTFChars(tempFilePathJStr, nullptr);
  std::string tempFilePath = pathChars;
  env->ReleaseStringUTFChars(tempFilePathJStr, pathChars);

  // HeifWriter.Builder(path, width, height, INPUT_MODE_BITMAP)
  // INPUT_MODE_BITMAP = 2
  auto Builder_init =
      env->GetMethodID(HeifWriterBuilderClass, "<init>", "(Ljava/lang/String;III)V");
  auto Builder_setQuality = env->GetMethodID(HeifWriterBuilderClass, "setQuality",
                                             "(I)Landroidx/heifwriter/HeifWriter$Builder;");
  auto Builder_setMaxImages = env->GetMethodID(HeifWriterBuilderClass, "setMaxImages",
                                               "(I)Landroidx/heifwriter/HeifWriter$Builder;");
  auto Builder_build =
      env->GetMethodID(HeifWriterBuilderClass, "build", "()Landroidx/heifwriter/HeifWriter;");

  auto builder = env->NewObject(HeifWriterBuilderClass, Builder_init,
                                env->NewStringUTF(tempFilePath.c_str()), width, height, 2);
  if (builder == nullptr || env->ExceptionCheck()) {
    env->ExceptionClear();
    env->CallBooleanMethod(tempFile, File_delete);
    return nullptr;
  }

  env->CallObjectMethod(builder, Builder_setQuality, quality);
  env->CallObjectMethod(builder, Builder_setMaxImages, 1);

  // Note: HeifWriter does not support setting color space. It always outputs sRGB.
  // Non-sRGB color spaces (like Display P3) will be converted to sRGB during encoding.

  auto HeifWriterClass = env->FindClass("androidx/heifwriter/HeifWriter");
  auto HeifWriter_start = env->GetMethodID(HeifWriterClass, "start", "()V");
  auto HeifWriter_addBitmap =
      env->GetMethodID(HeifWriterClass, "addBitmap", "(Landroid/graphics/Bitmap;)V");
  auto HeifWriter_stop = env->GetMethodID(HeifWriterClass, "stop", "(J)V");
  auto HeifWriter_close = env->GetMethodID(HeifWriterClass, "close", "()V");

  auto heifWriter = env->CallObjectMethod(builder, Builder_build);
  if (heifWriter == nullptr || env->ExceptionCheck()) {
    env->ExceptionClear();
    env->CallBooleanMethod(tempFile, File_delete);
    return nullptr;
  }

  // Create Bitmap from pixmap
  auto srcInfo = pixmap.info();
  Buffer tempBuffer = {};
  const void* pixels = pixmap.pixels();
  // HeifWriter only supports sRGB, convert color space and color type if necessary
  auto dstColorSpace = ColorSpace::SRGB();
  auto needsConversion = srcInfo.colorType() != ColorType::RGBA_8888 ||
                         !ColorSpace::Equals(srcInfo.colorSpace().get(), dstColorSpace.get());
  auto info = srcInfo;
  if (needsConversion) {
    info = ImageInfo::Make(srcInfo.width(), srcInfo.height(), ColorType::RGBA_8888,
                           srcInfo.alphaType(), 0, dstColorSpace);
    tempBuffer.alloc(info.byteSize());
    if (!pixmap.readPixels(info, tempBuffer.data())) {
      env->CallVoidMethod(heifWriter, HeifWriter_close);
      env->CallBooleanMethod(tempFile, File_delete);
      return nullptr;
    }
    pixels = tempBuffer.data();
  }

  // Bitmap.Config.ARGB_8888
  auto BitmapConfigClass = env->FindClass("android/graphics/Bitmap$Config");
  auto ARGB_8888_field =
      env->GetStaticFieldID(BitmapConfigClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
  auto argb8888Config = env->GetStaticObjectField(BitmapConfigClass, ARGB_8888_field);

  // Create Bitmap without ColorSpace (HeifWriter handles color space metadata)
  auto bitmap = env->CallStaticObjectMethod(BitmapClass.get(), Bitmap_createBitmap, width, height,
                                            argb8888Config);
  if (bitmap == nullptr || env->ExceptionCheck()) {
    env->ExceptionClear();
    env->CallVoidMethod(heifWriter, HeifWriter_close);
    env->CallBooleanMethod(tempFile, File_delete);
    return nullptr;
  }

  // Copy pixels to bitmap using copyPixelsFromBuffer
  auto ByteBufferClass = env->FindClass("java/nio/ByteBuffer");
  auto ByteBuffer_wrap =
      env->GetStaticMethodID(ByteBufferClass, "wrap", "([B)Ljava/nio/ByteBuffer;");
  auto Bitmap_copyPixelsFromBuffer =
      env->GetMethodID(BitmapClass.get(), "copyPixelsFromBuffer", "(Ljava/nio/Buffer;)V");

  auto byteSize = static_cast<jsize>(info.byteSize());
  auto byteArray = env->NewByteArray(byteSize);
  env->SetByteArrayRegion(byteArray, 0, byteSize, static_cast<const jbyte*>(pixels));
  auto byteBuffer = env->CallStaticObjectMethod(ByteBufferClass, ByteBuffer_wrap, byteArray);
  env->CallVoidMethod(bitmap, Bitmap_copyPixelsFromBuffer, byteBuffer);

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    env->CallVoidMethod(heifWriter, HeifWriter_close);
    env->CallBooleanMethod(tempFile, File_delete);
    return nullptr;
  }

  // Encode using HeifWriter
  env->CallVoidMethod(heifWriter, HeifWriter_start);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    env->CallVoidMethod(heifWriter, HeifWriter_close);
    env->CallBooleanMethod(tempFile, File_delete);
    return nullptr;
  }

  env->CallVoidMethod(heifWriter, HeifWriter_addBitmap, bitmap);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    env->CallVoidMethod(heifWriter, HeifWriter_close);
    env->CallBooleanMethod(tempFile, File_delete);
    return nullptr;
  }

  // stop(0) means wait indefinitely
  env->CallVoidMethod(heifWriter, HeifWriter_stop, 0LL);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    env->CallVoidMethod(heifWriter, HeifWriter_close);
    env->CallBooleanMethod(tempFile, File_delete);
    return nullptr;
  }

  env->CallVoidMethod(heifWriter, HeifWriter_close);

  // Read the output file
  std::shared_ptr<Data> result = nullptr;
  FILE* file = fopen(tempFilePath.c_str(), "rb");
  if (file != nullptr) {
    fseek(file, 0, SEEK_END);
    auto fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize > 0) {
      auto fileSizeU = static_cast<size_t>(fileSize);
      Buffer fileBuffer = {};
      fileBuffer.alloc(fileSizeU);
      auto readSize = fread(fileBuffer.data(), 1, fileSizeU, file);
      if (readSize == fileSizeU) {
        result = Data::MakeWithCopy(fileBuffer.data(), fileSizeU);
      }
    }
    fclose(file);
  }

  // Delete temporary file
  env->CallBooleanMethod(tempFile, File_delete);

  return result;
}
}  // namespace tgfx
