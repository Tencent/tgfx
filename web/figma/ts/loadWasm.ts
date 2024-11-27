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
import Figma from '../wasm-mt/figma.js';
import * as types from "../../types/types"; // 导入 FigmaRenderer
import {TGFXBind} from '../../lib/tgfx.js';

if (typeof window !== 'undefined') {
    window.onload = async () => {
        try {
            const figma = await Figma({
                locateFile: (file) => './wasm-mt/' + file
            }).then((module: types.TGFX) => {
                TGFXBind(module);
                return module;
            }).catch((error: any) => {
                console.error(error);
                throw new Error("Hello2D init failed. Please check the .wasm file path!.");
            });


            // 发送自定义事件，传递 figma 模块
            const event = new CustomEvent('wasmLoaded', {detail: figma});
            document.dispatchEvent(event);
        } catch (error) {
            console.error(error);
            throw new Error("TGFX init failed. Please check the .wasm file path!.");
        }
    }
}