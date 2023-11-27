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

import {TGFXModule} from '../tgfx-module';

export function wasmAwaitRewind(constructor: any) {
    const ignoreStaticFunctions = ['length', 'name', 'prototype', 'wasmAsyncMethods'];
    let staticFunctions = Object.getOwnPropertyNames(constructor).filter(
        (name) => ignoreStaticFunctions.indexOf(name) === -1,
    );
    if (constructor.wasmAsyncMethods && constructor.wasmAsyncMethods.length > 0) {
        staticFunctions = staticFunctions.filter((name) => constructor.wasmAsyncMethods.indexOf(name) === -1);
    }

    let functions = Object.getOwnPropertyNames(constructor.prototype).filter(
        (name) => name !== 'constructor' && typeof constructor.prototype[name] === 'function',
    );
    if (constructor.prototype.wasmAsyncMethods && constructor.prototype.wasmAsyncMethods.length > 0) {
        functions = functions.filter((name) => constructor.prototype.wasmAsyncMethods.indexOf(name) === -1);
    }

    const proxyFn = (target: { [prop: string]: (...args: any[]) => any }, methodName: string) => {
        const fn = target[methodName];
        target[methodName] = function (...args) {
            if (TGFXModule.Asyncify.currData !== null) {
                const currData = TGFXModule.Asyncify.currData;
                TGFXModule.Asyncify.currData = null;
                const ret = fn.call(this, ...args);
                TGFXModule.Asyncify.currData = currData;
                return ret;
            } else {
                return fn.call(this, ...args);
            }
        };
    };

    staticFunctions.forEach((name) => proxyFn(constructor, name));
    functions.forEach((name) => proxyFn(constructor.prototype, name));
}

export function wasmAsyncMethod(target: any, propertyKey: string, descriptor: PropertyDescriptor) {
    if (!target.wasmAsyncMethods) {
        target.wasmAsyncMethods = [];
    }
    target.wasmAsyncMethods.push(propertyKey);
}

export function destroyVerify(constructor: any) {
    let functions = Object.getOwnPropertyNames(constructor.prototype).filter(
        (name) => name !== 'constructor' && typeof constructor.prototype[name] === 'function',
    );

    const proxyFn = (target: { [prop: string]: any }, methodName: string) => {
        const fn = target[methodName];
        target[methodName] = function (...args: any[]) {
            if (this['isDestroyed']) {
                console.error(`Don't call ${methodName} of the ${constructor.name} that is destroyed.`);
                return;
            }
            return fn.call(this, ...args);
        };
    };
    functions.forEach((name) => proxyFn(constructor.prototype, name));
}
