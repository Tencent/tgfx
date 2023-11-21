/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/opengl/GLFunctions.h"
#include "tgfx/utils/Buffer.h"
#include "tgfx/utils/Stream.h"

namespace tgfx {
static const std::string OUT_ROOT = ProjectPath::Absolute("test/out/");
static const std::string WEBP_FILE_EXT = ".webp";

bool CreateGLTexture(Context* context, int width, int height, GLTextureInfo* texture) {
  texture->target = GL_TEXTURE_2D;
  texture->format = GL_RGBA8;
  auto gl = GLFunctions::Get(context);
  gl->genTextures(1, &texture->id);
  if (texture->id <= 0) {
    return false;
  }
  gl->bindTexture(texture->target, texture->id);
  gl->texParameteri(texture->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(texture->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->texImage2D(texture->target, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  gl->bindTexture(texture->target, 0);
  return true;
}

std::shared_ptr<ImageCodec> MakeImageCodec(const std::string& path) {
  return ImageCodec::MakeFrom(ProjectPath::Absolute(path));
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
  std::filesystem::path path = OUT_ROOT + "/" + key + WEBP_FILE_EXT;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out.write(reinterpret_cast<const char*>(data->data()),
            static_cast<std::streamsize>(data->size()));
  out.close();
}

void SaveImage(const std::shared_ptr<PixelBuffer> pixelBuffer, const std::string& key) {
  if (pixelBuffer == nullptr) {
    return;
  }
  auto pixels = pixelBuffer->lockPixels();
  SaveImage(Pixmap(pixelBuffer->info(), pixels), key);
  pixelBuffer->unlockPixels();
}

void SaveImage(const Bitmap& bitmap, const std::string& key) {
  if (bitmap.isEmpty()) {
    return;
  }
  SaveImage(Pixmap(bitmap), key);
}

void SaveImage(const Pixmap& pixmap, const std::string& key) {
  auto data = ImageCodec::Encode(pixmap, EncodedFormat::WEBP, 100);
  if (data == nullptr) {
    return;
  }
  SaveFile(data, key);
}

void RemoveImage(const std::string& key) {
  std::filesystem::remove(OUT_ROOT + "/" + key + WEBP_FILE_EXT);
}
}  // namespace tgfx