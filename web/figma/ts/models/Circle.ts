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
// js/models/Circle.ts

import BaseElement from './BaseElement.js';

export default class Circle extends BaseElement {
    /**
     * 创建圆形元素
     */
    constructor() {
        super('circle');
        // 默认属性
        this.setAttribute('cx', this.getRandomInt(0, 1000).toString());
        this.setAttribute('cy', this.getRandomInt(0, 800).toString());
        this.setAttribute('r', this.getRandomInt(10, 100).toString());
        this.setAttribute('fill', this.getRandomColor());
    }

    /**
     * 获取随机整数
     * @param min - 最小值
     * @param max - 最大值
     * @returns 随机整数
     */
    getRandomInt(min: number, max: number): number {
        return Math.floor(Math.random() * (max - min + 1)) + min;
    }

    /**
     * 获取随机颜色
     * @returns 随机颜色
     */
    getRandomColor(): string {
        const letters = '0123456789ABCDEF';
        let color = '#';
        for (let i = 0; i < 6; i++) {
            color += letters[Math.floor(Math.random() * 16)];
        }
        return color;
    }
}