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

import * as types from '../types/types';
import {TGFXBind} from '../lib/tgfx';
import Hello2D from './wasm/hello2d';
import {
    ShareData,
    updateSize,
    onResizeEvent,
    onClickEvent,
    animationLoop,
    setupVisibilityListeners,
    loadImage,
    bindCanvasZoomAndPanEvents
} from "./common";

let shareData: ShareData = new ShareData();

if (typeof window !== 'undefined') {
    window.onload = async () => {
        try {
            shareData.Hello2DModule = await Hello2D({ locateFile: (file: string) => './wasm/' + file });
            TGFXBind(shareData.Hello2DModule);

            let tgfxView = shareData.Hello2DModule.TGFXView.MakeFrom('#hello2d');
            shareData.tgfxBaseView = tgfxView;
            var image = await loadImage("resources/assets/bridge.jpg");
            tgfxView.setImagePath("bridge",image);
            image = await loadImage("resources/assets/tgfx.png");
            tgfxView.setImagePath("TGFX",image);

            var font = new FontFace('default', "url(resources/font/NotoSansSC-Regular.otf)");
            font.load().then((loadedFont) => {
                document.fonts.add(loadedFont);
            })
            var emojiFont = new FontFace('emoji', "url(resources/font/NotoColorEmoji.ttf)");
            emojiFont.load().then((loadedFont) => {
                document.fonts.add(loadedFont);
            })
            tgfxView.registerFonts();

            updateSize(shareData);
            tgfxView.updateLayerTree(shareData.drawIndex);
            tgfxView.updateZoomScaleAndOffset(1.0, 0, 0);
            const canvas = document.getElementById('hello2d');
            bindCanvasZoomAndPanEvents(canvas, shareData);
            animationLoop(shareData);
            setupVisibilityListeners(shareData);
        } catch (error) {
            console.error(error);
            throw new Error("Hello2D init failed. Please check the .wasm file path!.");
        }
    };

    window.onresize = () => {
        onResizeEvent(shareData);
     };

    window.onclick = () => {
        onClickEvent(shareData);
    };
}

