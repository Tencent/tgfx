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
import Figma from './wasm-mt/Figma';

if (typeof window !== 'undefined') {
    window.onload = async () => {
        console.log('------------start  load--------\n');
        const figma = await Figma({
            locateFile: (file: string) => './wasm-mt/' + file
        }).then((module: any) => {
            console.log('------------load success --------\n');
            TGFXBind(module);
            return module;
        }).catch((error: any) => {
            console.log('------------load failed --------\n');
            console.error(error);
            throw new Error("TGFX init failed. Please check the .wasm file path!.");
        });
    };

    window.onresize = () => {
    };

    window.onclick = () => {
    };
}

