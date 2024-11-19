// js/models/Circle.js

import BaseElement from './BaseElement.js';

export default class Circle extends BaseElement {
    /**
     * 创建圆形元素
     */
    constructor() {
        super('circle');
        // 默认属性
        this.setAttribute('cx', this.getRandomInt(0, 1000));
        this.setAttribute('cy', this.getRandomInt(0, 800));
        this.setAttribute('r', this.getRandomInt(10, 100));
        this.setAttribute('fill', this.getRandomColor());
    }

    /**
     * 获取随机整数
     * @param {number} min 最小值
     * @param {number} max 最大值
     * @returns {number} 随机整数
     */
    getRandomInt(min, max) {
        return Math.floor(Math.random() * (max - min + 1)) + min;
    }

    /**
     * 获取随机颜色
     * @returns {string} 随机颜色
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