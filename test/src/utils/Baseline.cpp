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

#include "Baseline.h"
#include <CommonCrypto/CommonCrypto.h>
#include <fstream>
#include <unordered_set>
#include "base/TGFXTest.h"
#include "core/utils/MD5.h"
#include "core/utils/USE.h"
#include "nlohmann/json.hpp"
#include "tgfx/core/Data.h"
#include "tgfx/core/Surface.h"
#include "utils/ProjectPath.h"
#include "utils/TestUtils.h"

namespace tgfx {
static const std::string BASELINE_ROOT = ProjectPath::Absolute("test/baseline/");
static const std::string BASELINE_VERSION_PATH = BASELINE_ROOT + "/version.json";
static const std::string CACHE_MD5_PATH = BASELINE_ROOT + "/.cache/md5.json";
static const std::string CACHE_VERSION_PATH = BASELINE_ROOT + "/.cache/version.json";
static const std::string GIT_HEAD_PATH = BASELINE_ROOT + "/.cache/HEAD";
#ifdef GENERATE_BASELINE_IMAGES
static const std::string OUT_ROOT = ProjectPath::Absolute("test/baseline-out/");
#else
static const std::string OUT_ROOT = ProjectPath::Absolute("test/out/");
#endif
static const std::string OUT_MD5_PATH = OUT_ROOT + "/md5.json";
static const std::string OUT_VERSION_PATH = OUT_ROOT + "/version.json";

static nlohmann::json BaselineVersion = {};
static nlohmann::json CacheVersion = {};
static nlohmann::json OutputVersion = {};
static nlohmann::json CacheMD5 = {};
static nlohmann::json OutputMD5 = {};
static std::mutex jsonLocker = {};
static std::string currentVersion;

std::string DumpMD5(const void* bytes, size_t size) {
  auto digest = MD5::Calculate(bytes, size);
  char buffer[33];
  char* position = buffer;
  for (auto& i : digest) {
    snprintf(position, 3, "%02x", i);
    position += 2;
  }
  return {buffer, 32};
}

static nlohmann::json* FindJSON(nlohmann::json& md5JSON, const std::string& key,
                                std::string* lastKey) {
  std::vector<std::string> keys = {};
  size_t start;
  size_t end = 0;
  while ((start = key.find_first_not_of('/', end)) != std::string::npos) {
    end = key.find('/', start);
    keys.push_back(key.substr(start, end - start));
  }
  *lastKey = keys.back();
  keys.pop_back();
  auto json = &md5JSON;
  for (auto& jsonKey : keys) {
    if ((*json)[jsonKey] == nullptr) {
      (*json)[jsonKey] = {};
    }
    json = &(*json)[jsonKey];
  }
  return json;
}

static std::string GetJSONValue(nlohmann::json& target, const std::string& key) {
  std::lock_guard<std::mutex> autoLock(jsonLocker);
  std::string jsonKey;
  auto json = FindJSON(target, key, &jsonKey);
  auto value = (*json)[jsonKey];
  return value != nullptr ? value.get<std::string>() : "";
}

static void SetJSONValue(nlohmann::json& target, const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> autoLock(jsonLocker);
  std::string jsonKey;
  auto json = FindJSON(target, key, &jsonKey);
  (*json)[jsonKey] = value;
}

bool Baseline::Compare(std::shared_ptr<PixelBuffer> pixelBuffer, const std::string& key) {
  if (pixelBuffer == nullptr) {
    return false;
  }
  auto pixels = pixelBuffer->lockPixels();
  Pixmap pixmap(pixelBuffer->info(), pixels);
  auto result = Baseline::Compare(pixmap, key);
  pixelBuffer->unlockPixels();
  return result;
}

bool Baseline::Compare(const std::shared_ptr<Surface> surface, const std::string& key) {
  if (surface == nullptr) {
    return false;
  }
  Bitmap bitmap(surface->width(), surface->height(), false, false, surface->colorSpace());
  Pixmap pixmap(bitmap);
  auto result = surface->readPixels(pixmap.info(), pixmap.writablePixels());
  if (!result) {
    return false;
  }
  return Baseline::Compare(pixmap, key);
}

bool Baseline::Compare(const Bitmap& bitmap, const std::string& key) {
  if (bitmap.isEmpty()) {
    return false;
  }
  Pixmap pixmap(bitmap);
  return Baseline::Compare(pixmap, key);
}

static bool CompareVersionAndMd5(const std::string& md5, const std::string& key,
                                 const std::function<void(bool)>& callback) {
#ifdef UPDATE_BASELINE
  SetJSONValue(OutputMD5, key, md5);
  return true;
#endif
  auto baselineVersion = GetJSONValue(BaselineVersion, key);
  auto cacheVersion = GetJSONValue(CacheVersion, key);
  if (baselineVersion.empty() ||
      (baselineVersion == cacheVersion && GetJSONValue(CacheMD5, key) != md5)) {
    SetJSONValue(OutputVersion, key, currentVersion);
    SetJSONValue(OutputMD5, key, md5);
    if (callback) {
      callback(false);
    }
    return false;
  }
  SetJSONValue(OutputVersion, key, baselineVersion);
  if (callback) {
    callback(true);
  }
  return true;
}

bool Baseline::Compare(const Pixmap& pixmap, const std::string& key) {
  if (pixmap.isEmpty()) {
    return false;
  }
  std::string md5;
  if (pixmap.rowBytes() == pixmap.info().minRowBytes()) {
    md5 = DumpMD5(pixmap.pixels(), pixmap.byteSize());
  } else {
    Bitmap newBitmap(pixmap.width(), pixmap.height(), pixmap.isAlphaOnly(), false,
                     pixmap.colorSpace());
    Pixmap newPixmap(newBitmap);
    auto result = pixmap.readPixels(newPixmap.info(), newPixmap.writablePixels());
    if (!result) {
      return false;
    }
    md5 = DumpMD5(newPixmap.pixels(), newPixmap.byteSize());
  }
#ifdef GENERATE_BASELINE_IMAGES
  SaveImage(pixmap, key + "_base");
#endif
  return CompareVersionAndMd5(md5, key, [key, pixmap](bool result) {
    if (result) {
      RemoveImage(key);
    } else {
      SaveImage(pixmap, key);
    }
  });
}

bool Baseline::Compare(std::shared_ptr<Data> data, const std::string& key) {
  if (!data || static_cast<int>(data->size()) == 0) {
    return false;
  }
  std::string md5 = DumpMD5(data->data(), data->size());
  return CompareVersionAndMd5(md5, key, {});
}

void Baseline::SetUp() {
  std::ifstream cacheMD5File(CACHE_MD5_PATH);
  if (cacheMD5File.is_open()) {
    cacheMD5File >> CacheMD5;
    cacheMD5File.close();
  }
  std::ifstream baselineVersionFile(BASELINE_VERSION_PATH);
  if (baselineVersionFile.is_open()) {
    baselineVersionFile >> BaselineVersion;
    baselineVersionFile.close();
  }
  std::ifstream cacheVersionFile(CACHE_VERSION_PATH);
  if (cacheVersionFile.is_open()) {
    cacheVersionFile >> CacheVersion;
    cacheVersionFile.close();
  }
  std::ifstream headFile(GIT_HEAD_PATH);
  if (headFile.is_open()) {
    headFile >> currentVersion;
    headFile.close();
  }
}

static void RemoveEmptyFolder(const std::filesystem::path& path) {
  if (!std::filesystem::is_directory(path)) {
    if (path.filename() == ".DS_Store") {
      std::filesystem::remove(path);
    }
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    RemoveEmptyFolder(entry.path());
  }
  if (std::filesystem::is_empty(path)) {
    std::filesystem::remove(path);
  }
}

static void CreateFolder(const std::string& path) {
  std::filesystem::path filePath = path;
  std::filesystem::create_directories(filePath.parent_path());
}

void Baseline::TearDown() {
#ifdef UPDATE_BASELINE
  if (!TGFXTest::HasFailure()) {
#ifdef GENERATE_BASELINE_IMAGES
    auto outPath = ProjectPath::Absolute("test/out/");
    std::filesystem::remove_all(outPath);
    if (std::filesystem::exists(OUT_ROOT)) {
      std::filesystem::rename(OUT_ROOT, outPath);
    }
#endif
    CreateFolder(CACHE_MD5_PATH);
    std::ofstream outMD5File(CACHE_MD5_PATH);
    outMD5File << std::setw(4) << OutputMD5 << std::endl;
    outMD5File.close();
    CreateFolder(CACHE_VERSION_PATH);
    std::filesystem::copy(BASELINE_VERSION_PATH, CACHE_VERSION_PATH,
                          std::filesystem::copy_options::overwrite_existing);
  } else {
    std::filesystem::remove_all(OUT_ROOT);
  }
  USE(RemoveEmptyFolder);
#else
  std::filesystem::remove(OUT_MD5_PATH);
  if (!OutputMD5.empty()) {
    CreateFolder(OUT_MD5_PATH);
    std::ofstream outMD5File(OUT_MD5_PATH);
    outMD5File << std::setw(4) << OutputMD5 << std::endl;
    outMD5File.close();
  }
  CreateFolder(OUT_VERSION_PATH);
  std::ofstream versionFile(OUT_VERSION_PATH);
  versionFile << std::setw(4) << OutputVersion << std::endl;
  versionFile.close();
  RemoveEmptyFolder(OUT_ROOT);
#endif
}
}  // namespace tgfx
