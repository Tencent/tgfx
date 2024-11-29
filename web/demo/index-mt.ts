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

async function MakeFromFile(filePath: string): Promise<Uint8Array | null> {
    try {
        const response = await fetch(filePath);
        if (!response.ok) {
            return null;
        }
        const buffer = await response.arrayBuffer();
        return new Uint8Array(buffer);
    } catch (error) {
        console.error('Error loading file:', error);
        return null;
    }
}

if (typeof window !== 'undefined') {
    (window as any).MakeFromFile = MakeFromFile;
    window.onload = async () => {
        try {
            shareData.Hello2DModule = await Hello2D({ locateFile: (file: string) => './wasm-mt/' + file });
            TGFXBind(shareData.Hello2DModule);

            let tgfxView = shareData.Hello2DModule.TGFXThreadsView.MakeFrom('#hello2d');
            shareData.tgfxBaseView = tgfxView;
            var imagePath = "http://localhost:8081/../../resources/assets/bridge.jpg";
            await tgfxView.setImagePath(imagePath);

            var fontPath = "http://localhost:8081/../../resources/font/NotoSansSC-Regular.otf";
            var emojiFontPath = "http://localhost:8081/../../resources/font/NotoColorEmoji.ttf";
            await tgfxView.registerFonts(fontPath, emojiFontPath);
            updateSize(shareData);
        } catch (error) {
            console.error(error);
            throw new Error("Hello2D init failed. Please check the .wasm file path!.");
        }
    };

    window.onresize = () => {
        onresizeEvent(shareData);
        window.setTimeout(updateSize(shareData), 300);
    };

    window.onclick = () => {
        onclickEvent(shareData);
    };
}
