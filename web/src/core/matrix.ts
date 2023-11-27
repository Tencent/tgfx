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

import {destroyVerify, wasmAwaitRewind} from '../utils/decorators';
import {MatrixIndex} from '../types';

@destroyVerify
@wasmAwaitRewind
export class Matrix {
    public wasmIns;
    public isDestroyed = false;

    public constructor(wasmIns: any) {
        this.wasmIns = wasmIns;
    }

    /**
     * scaleX; horizontal scale factor to store
     */
    public get a(): number {
        return this.wasmIns ? this.wasmIns._get(MatrixIndex.a) : 0;
    }

    public set a(value: number) {
        this.wasmIns?._set(MatrixIndex.a, value);
    }

    /**
     * skewY; vertical skew factor to store
     */
    public get b(): number {
        return this.wasmIns ? this.wasmIns._get(MatrixIndex.b) : 0;
    }

    public set b(value: number) {
        this.wasmIns?._set(MatrixIndex.b, value);
    }

    /**
     * skewX; horizontal skew factor to store
     */
    public get c(): number {
        return this.wasmIns ? this.wasmIns._get(MatrixIndex.c) : 0;
    }

    public set c(value: number) {
        this.wasmIns?._set(MatrixIndex.c, value);
    }

    /**
     * scaleY; vertical scale factor to store
     */
    public get d(): number {
        return this.wasmIns ? this.wasmIns._get(MatrixIndex.d) : 0;
    }

    public set d(value: number) {
        this.wasmIns?._set(MatrixIndex.d, value);
    }

    /**
     * transX; horizontal translation to store
     */
    public get tx(): number {
        return this.wasmIns ? this.wasmIns._get(MatrixIndex.tx) : 0;
    }

    public set tx(value: number) {
        this.wasmIns?._set(MatrixIndex.tx, value);
    }

    /**
     * transY; vertical translation to store
     */
    public get ty(): number {
        return this.wasmIns ? this.wasmIns._get(MatrixIndex.ty) : 0;
    }

    public set ty(value: number) {
        this.wasmIns?._set(MatrixIndex.ty, value);
    }

    /**
     * Returns one matrix value.
     */
    public get(index: MatrixIndex): number {
        return this.wasmIns ? this.wasmIns._get(index) : 0;
    }

    /**
     * Sets Matrix value.
     */
    public set(index: MatrixIndex, value: number) {
        this.wasmIns?._set(index, value);
    }

    public destroy() {
        this.wasmIns.delete();
    }
}