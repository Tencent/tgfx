/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
