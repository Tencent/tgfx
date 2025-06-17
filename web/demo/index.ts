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

import * as types from '../types/types';
import {TGFXBind} from '../lib/tgfx';
import Hello2D from './wasm-mt/hello2d';
import {ShareData, updateSize, onresizeEvent, onclickEvent, loadImage} from "./common";

let shareData: ShareData = new ShareData();

if (typeof window !== 'undefined') {
    window.onload = async () => {
        try {
            shareData.Hello2DModule = await Hello2D({
                locateFile: (file: string) => './wasm-mt/' + file ,
                mainScriptUrlOrBlob: './wasm-mt/hello2d.js'
            });
            TGFXBind(shareData.Hello2DModule);

            let tgfxView = shareData.Hello2DModule.TGFXThreadsView.MakeFrom('#hello2d');
            shareData.tgfxBaseView = tgfxView;
            var imagePath = "http://localhost:8081/../../resources/assets/bridge.jpg";
            await tgfxView.setImagePath(imagePath);

            var fontPath = "../../resources/font/NotoSansSC-Regular.otf";
            const fontBuffer = await fetch(fontPath).then((response) => response.arrayBuffer());
            const fontUIntArray = new Uint8Array(fontBuffer);
            var emojiFontPath = "../../resources/font/NotoColorEmoji.ttf";
            const emojiFontBuffer = await fetch(emojiFontPath).then((response) => response.arrayBuffer());
            const emojiFontUIntArray = new Uint8Array(emojiFontBuffer);
            tgfxView.registerFonts(fontUIntArray, emojiFontUIntArray);
            updateSize(shareData);
            const canvas = document.getElementById('hello2d');
            if (canvas) {
                let lastMouse = {x: 0, y: 0};
                canvas.addEventListener('mousedown', (e) => {
                    lastMouse.x = e.offsetX;
                    lastMouse.y = e.offsetY;
                    (canvas as any)._dragging = true;
                });
                window.addEventListener('mousemove', (e) => {
                    if ((canvas as any)._dragging) {
                        let dx = e.movementX;
                        let dy = e.movementY;
                        shareData.offsetX += dx / shareData.zoom;
                        shareData.offsetY += dy / shareData.zoom;
                        shareData.tgfxBaseView.draw(shareData.drawIndex, shareData.zoom, shareData.offsetX, shareData.offsetY);
                    }
                });
                window.addEventListener('mouseup', () => {
                    (canvas as any)._dragging = false;
                });

                canvas.addEventListener('wheel', (e: WheelEvent) => {
                    e.preventDefault();
                    if (e.ctrlKey || e.metaKey) {
                        const zoomFactor = Math.exp(-e.deltaY * 0.001); // 更自然的缩放
                        const oldZoom = shareData.zoom;
                        const newZoom = Math.max(0.01, Math.min(100, oldZoom * zoomFactor));
                        const rect = canvas.getBoundingClientRect();
                        const px = (e.clientX - rect.left) * window.devicePixelRatio;
                        const py = (e.clientY - rect.top) * window.devicePixelRatio;
                        shareData.offsetX = (shareData.offsetX - px) * (newZoom / oldZoom) + px;
                        shareData.offsetY = (shareData.offsetY - py) * (newZoom / oldZoom) + py;
                        shareData.zoom = newZoom;
                        shareData.tgfxBaseView.draw(shareData.drawIndex, shareData.zoom, shareData.offsetX, shareData.offsetY);
                    } else {
                        shareData.offsetX -= e.deltaX / shareData.zoom;
                        shareData.offsetY -= e.deltaY / shareData.zoom;
                        shareData.tgfxBaseView.draw(shareData.drawIndex, shareData.zoom, shareData.offsetX, shareData.offsetY);
                    }
                }, {passive: false});
            }
        } catch (error) {
            console.error(error);
            throw new Error("Hello2D init failed. Please check the .wasm file path!.");
        }
    };

    window.onresize = () => {
        onresizeEvent(shareData);
        window.setTimeout(() => updateSize(shareData), 300);
    };

    window.onclick = () => {
        onclickEvent(shareData);
    };
}
