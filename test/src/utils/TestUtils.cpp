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

#include "TestUtils.h"
#include <dirent.h>
#include <fstream>
#include "ProjectPath.h"
#include "core/utils/Log.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Stream.h"
#include "tgfx/gpu/opengl/GLFunctions.h"

namespace tgfx {
#ifdef GENERATE_BASELINE_IMAGES
static const std::string OUT_ROOT = ProjectPath::Absolute("test/baseline-out/");
#else
static const std::string OUT_ROOT = ProjectPath::Absolute("test/out/");
#endif
static const std::string WEBP_FILE_EXT = ".webp";

unsigned LoadGLShader(const GLFunctions* gl, unsigned shaderType, const std::string& source) {
  auto shader = gl->createShader(shaderType);
  const char* files[] = {source.c_str()};
  gl->shaderSource(shader, 1, files, nullptr);
  gl->compileShader(shader);
  int success;
  gl->getShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    gl->getShaderInfoLog(shader, 512, nullptr, infoLog);
    LOGE("Could not compile shader:\n%s\ntype:%d info%s", source.c_str(), shaderType, infoLog);
    gl->deleteShader(shader);
    shader = 0;
  }
  return shader;
}

unsigned CreateGLProgram(Context* context, const std::string& vertex, const std::string& fragment) {
  auto gl = GLFunctions::Get(context);
  auto vertexShader = LoadGLShader(gl, GL_VERTEX_SHADER, vertex);
  if (vertexShader == 0) {
    return 0;
  }
  auto fragmentShader = LoadGLShader(gl, GL_FRAGMENT_SHADER, fragment);
  if (fragmentShader == 0) {
    return 0;
  }
  auto programHandle = gl->createProgram();
  gl->attachShader(programHandle, vertexShader);
  gl->attachShader(programHandle, fragmentShader);
  gl->linkProgram(programHandle);
  int success;
  gl->getProgramiv(programHandle, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    gl->getProgramInfoLog(programHandle, 512, nullptr, infoLog);
    gl->deleteProgram(programHandle);
    programHandle = 0;
    LOGE("CreateGLProgram failed:%s", infoLog);
  }
  gl->deleteShader(vertexShader);
  gl->deleteShader(fragmentShader);
  return programHandle;
}

bool CreateGLTexture(Context* context, int width, int height, GLTextureInfo* texture) {
  texture->target = GL_TEXTURE_2D;
  texture->format = GL_RGBA8;
  auto gl = GLFunctions::Get(context);
  gl->genTextures(1, &texture->id);
  if (texture->id <= 0) {
    return false;
  }
  gl->bindTexture(texture->target, texture->id);
  gl->texImage2D(texture->target, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  gl->bindTexture(texture->target, 0);
  return true;
}

std::shared_ptr<ImageCodec> MakeImageCodec(const std::string& path) {
  return ImageCodec::MakeFrom(ProjectPath::Absolute(path));
}

std::shared_ptr<ImageCodec> MakeNativeCodec(const std::string& path) {
  return ImageCodec::MakeNativeCodec(ProjectPath::Absolute(path));
}

std::shared_ptr<Image> MakeImage(const std::string& path) {
  return Image::MakeFromFile(ProjectPath::Absolute(path));
}

std::shared_ptr<Typeface> MakeTypeface(const std::string& path) {
  return Typeface::MakeFromPath(ProjectPath::Absolute(path));
}

std::shared_ptr<Data> ReadFile(const std::string& path) {
  auto stream = Stream::MakeFromFile(ProjectPath::Absolute(path));
  if (stream == nullptr) {
    return nullptr;
  }
  Buffer buffer(stream->size());
  stream->read(buffer.data(), buffer.size());
  return buffer.release();
}

void SaveFile(std::shared_ptr<Data> data, const std::string& key) {
  std::filesystem::path path = OUT_ROOT + "/" + key;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out.write(reinterpret_cast<const char*>(data->data()),
            static_cast<std::streamsize>(data->size()));
  out.close();
}

void SaveWebpFile(std::shared_ptr<Data> data, const std::string& key) {
  SaveFile(data, key + WEBP_FILE_EXT);
}

void SaveImage(const std::shared_ptr<PixelBuffer> pixelBuffer, const std::string& key) {
  if (pixelBuffer == nullptr) {
    return;
  }
  auto pixels = pixelBuffer->lockPixels();
  SaveImage(Pixmap(pixelBuffer->info(), pixels), key, pixelBuffer->colorSpace());
  pixelBuffer->unlockPixels();
}

void SaveImage(const Bitmap& bitmap, const std::string& key) {
  if (bitmap.isEmpty()) {
    return;
  }
  SaveImage(Pixmap(bitmap), key, bitmap.colorSpace());
}

void SaveImage(const Pixmap& pixmap, const std::string& key,
               std::shared_ptr<ColorSpace> colorSpace) {
  auto data = ImageCodec::Encode(pixmap, EncodedFormat::WEBP, 100, colorSpace);
  if (data == nullptr) {
    return;
  }
  SaveWebpFile(data, key);
}

void RemoveImage(const std::string& key) {
  RemoveFile(key + WEBP_FILE_EXT);
}

void RemoveFile(const std::string& key) {
  std::filesystem::remove(OUT_ROOT + "/" + key);
}

std::shared_ptr<Image> ScaleImage(const std::shared_ptr<Image>& image, float scale,
                                  const SamplingOptions& options) {
  auto newWidth = static_cast<int>(roundf(scale * static_cast<float>(image->width())));
  auto newHeight = static_cast<int>(roundf(scale * static_cast<float>(image->height())));
  return image->makeScaled(newWidth, newHeight, options);
}

}  // namespace tgfx
