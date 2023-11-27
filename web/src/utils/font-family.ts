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

const fontNames = ['Arial', '"Courier New"', 'Georgia', '"Times New Roman"', '"Trebuchet MS"', 'Verdana'];

export const defaultFontNames = (() => {
    return ['emoji'].concat(...fontNames);
})();

export const getFontFamilies = (name: string, style = ''): string[] => {
    if (!name) return [];
    const nameChars = name.split(' ');
    let names = [];
    if (nameChars.length === 1) {
        names.push(name);
    } else {
        names.push(nameChars.join(''));
        names.push(nameChars.join(' '));
    }
    const fontFamilies = names.reduce((pre: string[], cur: string) => {
        if (!style) {
            pre.push(`"${cur}"`);
        } else {
            pre.push(`"${cur} ${style}"`);
            pre.push(`"${cur}-${style}"`);
        }
        return pre;
    }, []);
    // Fallback font when style is not found.
    if (style !== '') {
        fontFamilies.push(`"${name}"`);
    }
    return fontFamilies;
};