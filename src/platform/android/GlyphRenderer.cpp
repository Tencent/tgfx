/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GlyphRenderer.h"
#include <cstring>
#include "JNIUtil.h"
#include "tgfx/platform/android/Global.h"
#include "tgfx/platform/android/JNIEnvironment.h"

namespace tgfx {
// Bitmap class
static Global<jclass> BitmapClass;
static jmethodID Bitmap_createBitmap = nullptr;
static jmethodID Bitmap_getPixels = nullptr;
static jmethodID Bitmap_recycle = nullptr;

// Bitmap.Config class
static Global<jclass> BitmapConfigClass;
static jfieldID BitmapConfig_ARGB_8888 = nullptr;

// Canvas class
static Global<jclass> CanvasClass;
static jmethodID Canvas_Constructor = nullptr;
static jmethodID Canvas_drawText = nullptr;

// Paint class
static Global<jclass> PaintClass;
static jmethodID Paint_Constructor = nullptr;
static jmethodID Paint_setTextSize = nullptr;
static jmethodID Paint_setTypeface = nullptr;
static jmethodID Paint_setAntiAlias = nullptr;
static jmethodID Paint_getTextBounds = nullptr;
static jmethodID Paint_measureText = nullptr;
static jmethodID Paint_getFontMetrics = nullptr;

// Paint.FontMetrics class
static Global<jclass> FontMetricsClass;
static jfieldID FontMetrics_ascent = nullptr;
static jfieldID FontMetrics_descent = nullptr;
static jfieldID FontMetrics_leading = nullptr;

// Typeface class
static Global<jclass> TypefaceClass;
static jmethodID Typeface_createFromFile = nullptr;

// Rect class
static Global<jclass> RectClass;
static jmethodID Rect_Constructor = nullptr;
static jfieldID Rect_left = nullptr;
static jfieldID Rect_top = nullptr;
static jfieldID Rect_right = nullptr;
static jfieldID Rect_bottom = nullptr;

static bool initialized = false;

void GlyphRenderer::JNIInit(JNIEnv* env) {
  if (env == nullptr || initialized) {
    return;
  }

  // Bitmap
  auto bitmapClass = env->FindClass("android/graphics/Bitmap");
  if (bitmapClass == nullptr) {
    env->ExceptionClear();
    return;
  }
  BitmapClass.reset(bitmapClass);
  env->DeleteLocalRef(bitmapClass);

  Bitmap_createBitmap =
      env->GetStaticMethodID(BitmapClass.get(), "createBitmap",
                             "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
  Bitmap_getPixels = env->GetMethodID(BitmapClass.get(), "getPixels", "([IIIIIII)V");
  Bitmap_recycle = env->GetMethodID(BitmapClass.get(), "recycle", "()V");

  // Bitmap.Config
  auto configClass = env->FindClass("android/graphics/Bitmap$Config");
  BitmapConfigClass.reset(configClass);
  env->DeleteLocalRef(configClass);
  BitmapConfig_ARGB_8888 = env->GetStaticFieldID(BitmapConfigClass.get(), "ARGB_8888",
                                                 "Landroid/graphics/Bitmap$Config;");

  // Canvas
  auto canvasClass = env->FindClass("android/graphics/Canvas");
  CanvasClass.reset(canvasClass);
  env->DeleteLocalRef(canvasClass);
  Canvas_Constructor =
      env->GetMethodID(CanvasClass.get(), "<init>", "(Landroid/graphics/Bitmap;)V");
  Canvas_drawText = env->GetMethodID(CanvasClass.get(), "drawText",
                                     "(Ljava/lang/String;FFLandroid/graphics/Paint;)V");

  // Paint
  auto paintClass = env->FindClass("android/graphics/Paint");
  PaintClass.reset(paintClass);
  env->DeleteLocalRef(paintClass);
  Paint_Constructor = env->GetMethodID(PaintClass.get(), "<init>", "(I)V");
  Paint_setTextSize = env->GetMethodID(PaintClass.get(), "setTextSize", "(F)V");
  Paint_setTypeface = env->GetMethodID(PaintClass.get(), "setTypeface",
                                       "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;");
  Paint_setAntiAlias = env->GetMethodID(PaintClass.get(), "setAntiAlias", "(Z)V");
  Paint_getTextBounds = env->GetMethodID(PaintClass.get(), "getTextBounds",
                                         "(Ljava/lang/String;IILandroid/graphics/Rect;)V");
  Paint_measureText = env->GetMethodID(PaintClass.get(), "measureText", "(Ljava/lang/String;)F");
  Paint_getFontMetrics = env->GetMethodID(PaintClass.get(), "getFontMetrics",
                                          "()Landroid/graphics/Paint$FontMetrics;");

  // Paint.FontMetrics
  auto fontMetricsClass = env->FindClass("android/graphics/Paint$FontMetrics");
  FontMetricsClass.reset(fontMetricsClass);
  env->DeleteLocalRef(fontMetricsClass);
  FontMetrics_ascent = env->GetFieldID(FontMetricsClass.get(), "ascent", "F");
  FontMetrics_descent = env->GetFieldID(FontMetricsClass.get(), "descent", "F");
  FontMetrics_leading = env->GetFieldID(FontMetricsClass.get(), "leading", "F");

  // Typeface
  auto typefaceClass = env->FindClass("android/graphics/Typeface");
  TypefaceClass.reset(typefaceClass);
  env->DeleteLocalRef(typefaceClass);
  Typeface_createFromFile = env->GetStaticMethodID(
      TypefaceClass.get(), "createFromFile", "(Ljava/lang/String;)Landroid/graphics/Typeface;");

  // Rect
  auto rectClass = env->FindClass("android/graphics/Rect");
  RectClass.reset(rectClass);
  env->DeleteLocalRef(rectClass);
  Rect_Constructor = env->GetMethodID(RectClass.get(), "<init>", "()V");
  Rect_left = env->GetFieldID(RectClass.get(), "left", "I");
  Rect_top = env->GetFieldID(RectClass.get(), "top", "I");
  Rect_right = env->GetFieldID(RectClass.get(), "right", "I");
  Rect_bottom = env->GetFieldID(RectClass.get(), "bottom", "I");

  initialized = true;
}

bool GlyphRenderer::IsAvailable() {
  return initialized && BitmapClass.get() != nullptr && CanvasClass.get() != nullptr &&
         PaintClass.get() != nullptr;
}

static jobject CreatePaint(JNIEnv* env, const std::string& fontPath, float textSize) {
  // Paint.ANTI_ALIAS_FLAG = 1
  jobject paint = env->NewObject(PaintClass.get(), Paint_Constructor, 1);
  if (paint == nullptr) {
    return nullptr;
  }

  env->CallVoidMethod(paint, Paint_setTextSize, textSize);

  if (!fontPath.empty()) {
    jstring jPath = SafeToJString(env, fontPath);
    if (jPath != nullptr) {
      jobject typeface =
          env->CallStaticObjectMethod(TypefaceClass.get(), Typeface_createFromFile, jPath);
      env->DeleteLocalRef(jPath);
      if (typeface != nullptr && !env->ExceptionCheck()) {
        env->CallObjectMethod(paint, Paint_setTypeface, typeface);
        env->DeleteLocalRef(typeface);
      } else {
        env->ExceptionClear();
      }
    }
  }

  return paint;
}

bool GlyphRenderer::RenderGlyph(const std::string& fontPath, const std::string& text,
                                float textSize, int width, int height, float offsetX, float offsetY,
                                void* dstPixels) {
  if (!IsAvailable() || dstPixels == nullptr || width <= 0 || height <= 0 || text.empty()) {
    return false;
  }

  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return false;
  }

  bool success = false;
  jobject bitmap = nullptr;
  jobject canvas = nullptr;
  jobject paint = nullptr;
  jstring jText = nullptr;
  jintArray pixelArray = nullptr;

  do {
    jobject config = env->GetStaticObjectField(BitmapConfigClass.get(), BitmapConfig_ARGB_8888);
    if (config == nullptr) {
      break;
    }

    bitmap =
        env->CallStaticObjectMethod(BitmapClass.get(), Bitmap_createBitmap, width, height, config);
    env->DeleteLocalRef(config);
    if (bitmap == nullptr || env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }

    canvas = env->NewObject(CanvasClass.get(), Canvas_Constructor, bitmap);
    if (canvas == nullptr || env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }

    paint = CreatePaint(env, fontPath, textSize);
    if (paint == nullptr) {
      break;
    }

    // Draw text
    jText = SafeToJString(env, text);
    if (jText == nullptr) {
      break;
    }
    env->CallVoidMethod(canvas, Canvas_drawText, jText, offsetX, offsetY, paint);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }

    // Get pixels
    auto pixelCount = width * height;
    pixelArray = env->NewIntArray(pixelCount);
    if (pixelArray == nullptr) {
      break;
    }

    env->CallVoidMethod(bitmap, Bitmap_getPixels, pixelArray, 0, width, 0, 0, width, height);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }

    jint* pixels = env->GetIntArrayElements(pixelArray, nullptr);
    if (pixels == nullptr) {
      break;
    }

    auto* dst = static_cast<uint8_t*>(dstPixels);
    for (int i = 0; i < pixelCount; ++i) {
      uint32_t argb = static_cast<uint32_t>(pixels[i]);
      uint8_t a = (argb >> 24) & 0xFF;
      uint8_t r = (argb >> 16) & 0xFF;
      uint8_t g = (argb >> 8) & 0xFF;
      uint8_t b = argb & 0xFF;
      dst[i * 4 + 0] = r;
      dst[i * 4 + 1] = g;
      dst[i * 4 + 2] = b;
      dst[i * 4 + 3] = a;
    }

    env->ReleaseIntArrayElements(pixelArray, pixels, JNI_ABORT);
    success = true;
  } while (false);

  // Cleanup
  if (bitmap != nullptr) {
    env->CallVoidMethod(bitmap, Bitmap_recycle);
    env->DeleteLocalRef(bitmap);
  }
  if (canvas != nullptr) {
    env->DeleteLocalRef(canvas);
  }
  if (paint != nullptr) {
    env->DeleteLocalRef(paint);
  }
  if (jText != nullptr) {
    env->DeleteLocalRef(jText);
  }
  if (pixelArray != nullptr) {
    env->DeleteLocalRef(pixelArray);
  }

  return success;
}

bool GlyphRenderer::MeasureText(const std::string& fontPath, const std::string& text,
                                float textSize, float* bounds, float* advance) {
  if (!IsAvailable() || text.empty()) {
    return false;
  }

  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return false;
  }

  bool success = false;
  jobject paint = nullptr;
  jobject rect = nullptr;
  jstring jText = nullptr;

  do {
    paint = CreatePaint(env, fontPath, textSize);
    if (paint == nullptr) {
      break;
    }

    jText = SafeToJString(env, text);
    if (jText == nullptr) {
      break;
    }

    // Get text bounds
    if (bounds != nullptr) {
      rect = env->NewObject(RectClass.get(), Rect_Constructor);
      if (rect == nullptr) {
        break;
      }

      jint textLength = env->GetStringLength(jText);
      env->CallVoidMethod(paint, Paint_getTextBounds, jText, 0, textLength, rect);
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
        break;
      }

      bounds[0] = static_cast<float>(env->GetIntField(rect, Rect_left));
      bounds[1] = static_cast<float>(env->GetIntField(rect, Rect_top));
      bounds[2] = static_cast<float>(env->GetIntField(rect, Rect_right));
      bounds[3] = static_cast<float>(env->GetIntField(rect, Rect_bottom));
    }

    // Get advance
    if (advance != nullptr) {
      *advance = env->CallFloatMethod(paint, Paint_measureText, jText);
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
        break;
      }
    }

    success = true;
  } while (false);

  // Cleanup
  if (paint != nullptr) {
    env->DeleteLocalRef(paint);
  }
  if (rect != nullptr) {
    env->DeleteLocalRef(rect);
  }
  if (jText != nullptr) {
    env->DeleteLocalRef(jText);
  }

  return success;
}

bool GlyphRenderer::GetFontMetrics(const std::string& fontPath, float textSize, float* ascent,
                                   float* descent, float* leading) {
  if (!IsAvailable()) {
    return false;
  }

  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return false;
  }

  bool success = false;
  jobject paint = nullptr;
  jobject metrics = nullptr;

  do {
    paint = CreatePaint(env, fontPath, textSize);
    if (paint == nullptr) {
      break;
    }

    metrics = env->CallObjectMethod(paint, Paint_getFontMetrics);
    if (metrics == nullptr || env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }

    if (ascent != nullptr) {
      *ascent = env->GetFloatField(metrics, FontMetrics_ascent);
    }
    if (descent != nullptr) {
      *descent = env->GetFloatField(metrics, FontMetrics_descent);
    }
    if (leading != nullptr) {
      *leading = env->GetFloatField(metrics, FontMetrics_leading);
    }

    success = true;
  } while (false);

  // Cleanup
  if (paint != nullptr) {
    env->DeleteLocalRef(paint);
  }
  if (metrics != nullptr) {
    env->DeleteLocalRef(metrics);
  }

  return success;
}

}  // namespace tgfx
