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

#include "PDFSubsetFont.h"
#include "hb-subset-repacker.h"
#include "hb-subset.h"
#include "hb.h"
#include "pdf/PDFFont.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Stream.h"

namespace tgfx {

namespace {

using HBBlob = std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>;
using HBFace = std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>;
using HBSubsetInput = std::unique_ptr<hb_subset_input_t, decltype(&hb_subset_input_destroy)>;
using HBSet = std::unique_ptr<hb_set_t, decltype(&hb_set_destroy)>;

HBBlob StreamToBlob(std::unique_ptr<Stream> asset) {
  size_t size = asset->size();
  HBBlob blob(nullptr, hb_blob_destroy);
  if (const void* base = asset->getMemoryBase()) {
    blob.reset(hb_blob_create(const_cast<char*>(static_cast<const char*>(base)),
                              static_cast<uint32_t>(size), HB_MEMORY_MODE_READONLY, asset.release(),
                              [](void* p) { delete static_cast<Stream*>(p); }));
  } else {
    void* ptr = size ? std::malloc(size) : nullptr;
    asset->read(ptr, size);
    blob.reset(hb_blob_create(static_cast<char*>(ptr), static_cast<uint32_t>(size),
                              HB_MEMORY_MODE_READONLY, ptr, std::free));
  }
  DEBUG_ASSERT(blob);
  hb_blob_make_immutable(blob.get());
  return blob;
}

std::shared_ptr<Data> ToData(HBBlob blob) {
  if (!blob) {
    return nullptr;
  }
  unsigned int length;
  const char* data = hb_blob_get_data(blob.get(), &length);
  if (!data || !length) {
    return nullptr;
  }
  return Data::MakeAdopted(
      data, length, [](const void*, void* ctx) { hb_blob_destroy(static_cast<hb_blob_t*>(ctx)); },
      blob.release());
}

std::shared_ptr<Data> ExtractCFFData(const hb_face_t* face) {
  // hb_face_reference_table usually returns hb_blob_get_empty instead of nullptr.
  HBBlob cff(hb_face_reference_table(face, HB_TAG('C', 'F', 'F', ' ')), hb_blob_destroy);
  return ToData(std::move(cff));
}

HBFace MakeSubset(hb_subset_input_t* input, hb_face_t* face, bool retainZeroGlyph) {
  // TODO (YGaurora): When possible, check if a font is 'tricky' with FT_IS_TRICKY.
  // If it isn't known if a font is 'tricky', retain the hints.
  unsigned int flags = HB_SUBSET_FLAGS_RETAIN_GIDS;
  if (retainZeroGlyph) {
    flags |= HB_SUBSET_FLAGS_NOTDEF_OUTLINE;
  }
  hb_subset_input_set_flags(input, flags);
  return HBFace(hb_subset_or_fail(face, input), hb_face_destroy);
}

std::shared_ptr<Data> SubsetHarfbuzz(const std::shared_ptr<Typeface>& typeface,
                                     const PDFGlyphUse& glyphUsage) {
  auto typefaceStream = PDFFont::GetTypefaceStream(typeface);
  HBFace face(nullptr, hb_face_destroy);
  HBBlob blob(StreamToBlob(std::move(typefaceStream)));
  // hb_face_create always succeeds. Check that the format is minimally recognized first.
  // See https://github.com/harfbuzz/harfbuzz/issues/248
  uint32_t index = 0;
  unsigned int num_hb_faces = hb_face_count(blob.get());
  if (0 < num_hb_faces && index < num_hb_faces) {
    face.reset(hb_face_create(blob.get(), index));
    // Check the number of glyphs as a basic sanitization step.
    if (face && hb_face_get_glyph_count(face.get()) == 0) {
      face.reset();
    }
  }

  HBSubsetInput input(hb_subset_input_create_or_fail(), hb_subset_input_destroy);
  DEBUG_ASSERT(input);
  if (!face || !input) {
    return nullptr;
  }
  hb_set_t* glyphs = hb_subset_input_glyph_set(input.get());
  glyphUsage.getSetValues(
      [&glyphs](size_t gid) { hb_set_add(glyphs, static_cast<hb_codepoint_t>(gid)); });

  HBFace subset = MakeSubset(input.get(), face.get(), glyphUsage.has(0));
  if (!subset) {
    // Even if subsetting fails, extract CFF if available
    return ExtractCFFData(face.get());
  }

  HBBlob result(hb_face_reference_blob(subset.get()), hb_blob_destroy);
  return ToData(std::move(result));
}

}  // namespace

std::shared_ptr<Data> PDFSubsetFont(const std::shared_ptr<Typeface>& typeface,
                                    const PDFGlyphUse& glyphUsage) {
  return SubsetHarfbuzz(typeface, glyphUsage);
}

}  // namespace tgfx
