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
import Hello2D from './wasm/hello2d';
import {ShareData, updateSize, onresizeEvent, onclickEvent, loadImage} from "./common";

function mainLoop() {
    shareData.tgfxBaseView.flush();
    window.requestAnimationFrame(mainLoop);
}

let shareData: ShareData = new ShareData();

if (typeof window !== 'undefined') {
    window.onload = async () => {
        shareData.Hello2DModule = await Hello2D({locateFile: (file: string) => './wasm/' + file})
            .then((module: types.TGFX) => {
                TGFXBind(module);
                return module;
            })
            .catch((error: any) => {
                console.error(error);
                throw new Error("Hello2D init failed. Please check the .wasm file path!.");
            });
        let image = await loadImage('../../resources/assets/bridge.jpg');
        shareData.tgfxBaseView = shareData.Hello2DModule.TGFXView.MakeFrom('#hello2d', image);
        updateSize(shareData);
        window.requestAnimationFrame(mainLoop);
    };

    window.onresize = () => {
        onresizeEvent(shareData);
        window.setTimeout(updateSize(shareData), 300);
    };

    window.onclick = () => {
        onclickEvent(shareData);
    };
}

