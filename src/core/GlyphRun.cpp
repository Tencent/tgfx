/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/GlyphRun.h"
#include "core/FontGlyphFace.h"

namespace tgfx {
GlyphRun::GlyphRun(Font font, std::vector<GlyphID> glyphIDs, std::vector<Point> positions)
    : GlyphRun(FontGlyphFace::Make(font), glyphIDs, positions) {
    }

GlyphRun::GlyphRun(std::shared_ptr<GlyphFace> glyphFace, std::vector<GlyphID> glyphIDs,
           std::vector<Point> positions)
    : glyphFace(glyphFace), glyphs(glyphIDs), positions(positions) {
    }

}  // namespace tgfx