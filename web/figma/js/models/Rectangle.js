// js/models/Rectangle.js

import BaseElement from './BaseElement.js';

export default class Rectangle extends BaseElement {
    /**
     * 创建矩形元素
     */
    constructor() {
        super('rect');
        // 默认属性
        this.setAttribute('x', this.getRandomInt(0, 1000));
        this.setAttribute('y', this.getRandomInt(0, 800));
        this.setAttribute('width', this.getRandomInt(50, 200));
        this.setAttribute('height', this.getRandomInt(50, 200));
        this.setAttribute('fill', this.getRandomColor());
    }

    /**
     * 生成指定范围内的随机整数
     */
    getRandomInt(min, max) {
        return Math.floor(Math.random() * (max - min + 1)) + min;
    }

    /**
     * 生成随机颜色
     */
    getRandomColor() {
        const letters = '0123456789ABCDEF';
        let color = '#';
        for (let i = 0; i < 6; i++) {
            color += letters[Math.floor(Math.random() * 16)];
        }
        return color;
    }
}