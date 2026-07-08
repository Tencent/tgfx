/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
            if (!navigator.gpu) {
                throw new Error("WebGPU is not supported in this browser.");
            }
            const adapter = await navigator.gpu.requestAdapter();
            if (!adapter) {
                throw new Error("Failed to get WebGPU adapter.");
            }
            const device = await adapter.requestDevice();
            if (!device) {
                throw new Error("Failed to get WebGPU device.");
            }

            shareData.Hello2DModule = await Hello2D({
                locateFile: (file: string) => './wasm/' + file,
                preinitializedWebGPUDevice: device,
            });
            TGFXBind(shareData.Hello2DModule);

            let tgfxView = shareData.Hello2DModule.TGFXView.MakeFrom('#hello2d');
            shareData.tgfxBaseView = tgfxView;
            var image = await loadImage("resources/assets/bridge.jpg");
            tgfxView.setImagePath("bridge", image);
            image = await loadImage("resources/assets/tgfx.png");
            tgfxView.setImagePath("TGFX", image);

            var font = new FontFace('default', "url(resources/font/NotoSansSC-Regular.otf)");
            var emojiFont = new FontFace('emoji', "url(resources/font/NotoColorEmoji.ttf)");
            await Promise.all([font.load(), emojiFont.load()]).then(([loadedFont, loadedEmoji]) => {
                document.fonts.add(loadedFont);
                document.fonts.add(loadedEmoji);
            });
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
            document.body.innerHTML = `<div style="color:red;padding:20px;font-size:18px;">
                WebGPU initialization failed: ${error.message}<br><br>
                Please use Chrome 113+ with WebGPU enabled.
            </div>`;
        }
    };

    window.onresize = () => {
        onResizeEvent(shareData);
    };

    window.onclick = () => {
        onClickEvent(shareData);
    };
}
