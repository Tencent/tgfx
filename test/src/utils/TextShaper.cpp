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

#include "TextShaper.h"
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include "../../../include/tgfx/core/Log.h"
#include "hb.h"
#include "tgfx/core/Buffer.h"
#include "utils/ProjectPath.h"

namespace tgfx {
template <typename T>
struct PtrWrapper {
  explicit PtrWrapper(std::shared_ptr<T> ptr) : ptr(std::move(ptr)) {
  }
  std::shared_ptr<T> ptr;
};

hb_blob_t* HBGetTable(hb_face_t*, hb_tag_t tag, void* userData) {
  auto data = reinterpret_cast<PtrWrapper<Typeface>*>(userData)->ptr->copyTableData(tag);
  if (data == nullptr) {
    return nullptr;
  }
  auto wrapper = new PtrWrapper<Data>(data);
  return hb_blob_create(static_cast<const char*>(data->data()),
                        static_cast<unsigned int>(data->size()), HB_MEMORY_MODE_READONLY,
                        static_cast<void*>(wrapper),
                        [](void* ctx) { delete reinterpret_cast<PtrWrapper<Data>*>(ctx); });
}

std::shared_ptr<hb_face_t> CreateHBFace(const std::shared_ptr<Typeface>& typeface) {
  std::shared_ptr<hb_face_t> hbFace;
  auto stream = typeface->openStream();
  if (stream && stream->size() > 0) {
    auto size = stream->size();
    auto buffer = std::make_shared<Buffer>(size);
    if (stream->read(buffer->data(), size) != size) {
      return nullptr;
    }
    auto wrapper = new PtrWrapper<Buffer>(buffer);
    auto blob = std::shared_ptr<hb_blob_t>(
        hb_blob_create(static_cast<const char*>(buffer->data()),
                       static_cast<unsigned int>(buffer->size()), HB_MEMORY_MODE_READONLY,
                       static_cast<void*>(wrapper),
                       [](void* ctx) { delete reinterpret_cast<PtrWrapper<Buffer>*>(ctx); }),
        hb_blob_destroy);
    if (hb_blob_get_empty() == blob.get()) {
      return nullptr;
    }
    hb_blob_make_immutable(blob.get());
    if (hb_face_count(blob.get()) > 0) {
      hbFace = std::shared_ptr<hb_face_t>(hb_face_create(blob.get(), 0), hb_face_destroy);
      if (hb_face_get_empty() == hbFace.get() || hb_face_get_glyph_count(hbFace.get()) == 0) {
        hbFace = nullptr;
      }
    }
  }
  if (hbFace == nullptr) {
    auto wrapper = new PtrWrapper<Typeface>(typeface);
    hbFace = std::shared_ptr<hb_face_t>(
        hb_face_create_for_tables(
            HBGetTable, static_cast<void*>(wrapper),
            [](void* ctx) { delete reinterpret_cast<PtrWrapper<Typeface>*>(ctx); }),
        hb_face_destroy);
    if (hb_face_get_empty() == hbFace.get()) {
      return nullptr;
    }
    hb_face_set_index(hbFace.get(), 0);
  }
  if (hbFace == nullptr) {
    return nullptr;
  }
  hb_face_set_upem(hbFace.get(), static_cast<unsigned int>(typeface->unitsPerEm()));
  return hbFace;
}

class HBLockedFontCache {
 public:
  HBLockedFontCache(std::list<uint32_t>* lru, std::map<uint32_t, std::shared_ptr<hb_font_t>>* cache,
                    std::mutex* mutex)
      : lru(lru), cache(cache), mutex(mutex) {
    mutex->lock();
  }
  HBLockedFontCache(const HBLockedFontCache&) = delete;
  HBLockedFontCache& operator=(const HBLockedFontCache&) = delete;
  HBLockedFontCache& operator=(HBLockedFontCache&&) = delete;

  ~HBLockedFontCache() {
    mutex->unlock();
  }

  std::shared_ptr<hb_font_t> find(uint32_t fontId) {
    auto iter = std::find(lru->begin(), lru->end(), fontId);
    if (iter == lru->end()) {
      return nullptr;
    }
    lru->erase(iter);
    lru->push_front(fontId);
    return (*cache)[fontId];
  }
  std::shared_ptr<hb_font_t> insert(uint32_t fontId, std::shared_ptr<hb_font_t> hbFont) {
    if (hb_font_get_empty() == hbFont.get()) {
      return nullptr;
    }
    static const int MaxCacheSize = 100;
    cache->insert(std::make_pair(fontId, std::move(hbFont)));
    lru->push_front(fontId);
    while (lru->size() > MaxCacheSize) {
      auto id = lru->back();
      lru->pop_back();
      cache->erase(id);
    }
    return (*cache)[fontId];
  }
  void reset() {
    lru->clear();
    cache->clear();
  }

 private:
  std::list<uint32_t>* lru;
  std::map<uint32_t, std::shared_ptr<hb_font_t>>* cache;
  std::mutex* mutex;
};

static HBLockedFontCache GetHBFontCache() {
  static auto HBFontCacheMutex = new std::mutex();
  static auto HBFontLRU = new std::list<uint32_t>();
  static auto HBFontCache = new std::map<uint32_t, std::shared_ptr<hb_font_t>>();
  return {HBFontLRU, HBFontCache, HBFontCacheMutex};
}

static std::shared_ptr<hb_font_t> CreateHBFont(const std::shared_ptr<Typeface>& typeface) {
  auto cache = GetHBFontCache();
  auto hbFont = cache.find(typeface->uniqueID());
  if (hbFont == nullptr) {
    auto hbFace = CreateHBFace(typeface);
    if (hbFace == nullptr) {
      return nullptr;
    }
    hbFont = cache.insert(typeface->uniqueID(), std::shared_ptr<hb_font_t>(
                                                    hb_font_create(hbFace.get()), hb_font_destroy));
  }
  return hbFont;
}

static std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> ShapeText(
    const std::string& text, const std::shared_ptr<Typeface>& typeface) {
  auto hbFont = CreateHBFont(typeface);
  if (hbFont == nullptr) {
    return {};
  }

  auto hbBuffer = std::shared_ptr<hb_buffer_t>(hb_buffer_create(), hb_buffer_destroy);
  if (!hb_buffer_allocation_successful(hbBuffer.get())) {
    LOGI("TextShaper::shape text = %s, alloc harfbuzz(%p) failure", text.c_str(), hbBuffer.get());
    return {};
  }
  hb_buffer_add_utf8(hbBuffer.get(), text.data(), -1, 0, -1);
  hb_buffer_guess_segment_properties(hbBuffer.get());
  hb_shape(hbFont.get(), hbBuffer.get(), nullptr, 0);
  unsigned count = 0;
  auto infos = hb_buffer_get_glyph_infos(hbBuffer.get(), &count);
  std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> result;
  for (unsigned i = 0; i < count; ++i) {
    auto length = (i + 1 == count ? text.length() : infos[i + 1].cluster) - infos[i].cluster;
    if (length == 0) {
      continue;
    }
    result.emplace_back(infos[i].codepoint, infos[i].cluster, length);
  }
  return result;
}

struct HBGlyph {
  std::string text;
  GlyphID glyphID = 0;
  uint32_t stringIndex = 0;
  std::shared_ptr<Typeface> typeface;
};

static bool ShapeText(std::list<HBGlyph>& glyphs, std::shared_ptr<Typeface> typeface) {
  bool allShaped = true;
  for (auto iter = glyphs.begin(); iter != glyphs.end(); ++iter) {
    if (iter->glyphID == 0) {
      auto string = iter->text;
      auto infos = ShapeText(string, typeface);
      if (infos.empty()) {
        allShaped = false;
      } else {
        auto stringIndex = iter->stringIndex;
        for (size_t i = 0; i < infos.size(); ++i) {
          auto [codepoint, cluster, length] = infos[i];
          HBGlyph glyph;
          glyph.text = string.substr(cluster, length);
          glyph.stringIndex = stringIndex + cluster;
          if (codepoint != 0) {
            glyph.glyphID = static_cast<GlyphID>(codepoint);
            glyph.typeface = typeface;
          } else {
            allShaped = false;
          }
          if (i == 0) {
            iter = glyphs.erase(iter);
            iter = glyphs.insert(iter, glyph);
          } else {
            iter = glyphs.insert(++iter, glyph);
          }
        }
      }
    }
  }
  return allShaped;
}

static std::vector<std::shared_ptr<Typeface>> GetFallbackTypefaces() {
  std::vector<std::string> fontPaths = {
      ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"),
      ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf")};
  std::vector<std::shared_ptr<Typeface>> typefaces;
  for (const auto& fontPath : fontPaths) {
    auto typeface = Typeface::MakeFromPath(fontPath);
    if (typeface) {
      typefaces.push_back(typeface);
    }
  }
  return typefaces;
}

PositionedGlyphs TextShaper::Shape(const std::string& text, std::shared_ptr<Typeface> face) {
  std::list<HBGlyph> glyphs;
  glyphs.emplace_back(HBGlyph{text, {}, 0, nullptr});
  bool allShaped = false;
  if (face && !face->fontFamily().empty()) {
    allShaped = ShapeText(glyphs, std::move(face));
  }
  if (!allShaped) {
    static auto fallbackTypefaces = GetFallbackTypefaces();
    for (const auto& typeface : fallbackTypefaces) {
      if (typeface && ShapeText(glyphs, typeface)) {
        break;
      }
    }
  }
  std::vector<std::tuple<std::shared_ptr<Typeface>, GlyphID, uint32_t>> glyphIDs;
  for (const auto& glyph : glyphs) {
    glyphIDs.emplace_back(glyph.typeface, glyph.glyphID, glyph.stringIndex);
  }
  return PositionedGlyphs(std::move(glyphIDs));
}

void TextShaper::PurgeCaches() {
  auto cache = GetHBFontCache();
  cache.reset();
}
}  // namespace tgfx
