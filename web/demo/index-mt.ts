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

import * as types from '../types/types';
import {TGFXBind} from '../lib/tgfx';
import Hello2D from './wasm-mt/hello2d';
import {ShareData, updateSize, onresizeEvent, onclickEvent, loadImage} from "./common";

let shareData: ShareData = new ShareData();

class DataLoaderImpl {
    async makeFromFile(filePath: string): Promise<ArrayBuffer | null> {
        try {
            const response = await fetch(filePath);
            if (!response.ok) {
                return null;
            }
            return await response.arrayBuffer();
        } catch (error) {
            console.error('Error loading file:', error);
            return null;
        }
    }
}

const dataLoader = new DataLoaderImpl();

async function makeFromFile(filePath: string): Promise<ArrayBuffer | null> {
    return dataLoader.makeFromFile(filePath);
}
// Expose the function to the global scope
// (window as any).makeFromFile = makeFromFile;

if (typeof window !== 'undefined') {
    window.onload = async () => {
        shareData.Hello2DModule = await Hello2D({locateFile: (file: string) => './wasm-mt/' + file})
            .then((module: types.TGFX) => {
                TGFXBind(module);
                return module;
            })
            .catch((error: any) => {
                console.error(error);
                throw new Error("Hello2D init failed. Please check the .wasm file path!.");
            });

        let image = await loadImage('../../resources/assets/bridge.jpg');
        let tgfxView = shareData.Hello2DModule.TGFXThreadsView.MakeFrom('#hello2d', image);

        var fontPath = "../../resources/font/NotoSansSC-Regular.otf";
        const fontBuffer = await fetch(fontPath).then((response) => response.arrayBuffer());
        const fontUIntArray = new Uint8Array(fontBuffer);
        var emojiFontPath = "../../resources/font/NotoColorEmoji.ttf";
        const emojiFontBuffer = await fetch(emojiFontPath).then((response) => response.arrayBuffer());
        const emojiFontUIntArray = new Uint8Array(emojiFontBuffer);
        tgfxView.registerFonts(fontUIntArray, emojiFontUIntArray);

        shareData.tgfxBaseView = tgfxView;
        updateSize(shareData);
    };

    window.onresize = () => {
        onresizeEvent(shareData);
        window.setTimeout(updateSize(shareData), 300);
    };

    window.onclick = () => {
        onclickEvent(shareData);
    };
}

