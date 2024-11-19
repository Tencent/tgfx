// js/models/TextElement.js

import BaseElement from './BaseElement.js';

export default class TextElement extends BaseElement {
    /**
     * 创建文本元素
     */
    constructor() {
        super('text');
        // 默认属性
        this.setAttribute('x', this.getRandomInt(0, 1000));
        this.setAttribute('y', this.getRandomInt(0, 800));
        this.setAttribute('text-anchor', "text-anchor");
        this.setAttribute('dominant-baseline', "text-before-edge");
        this.setAttribute('fill', this.getRandomColor());
        this.setAttribute('font-size', this.getRandomInt(12, 36));
        this.setAttribute('font-family', this.getRandomFontFamily());
        this.element.textContent = this.getRandomText();
    }

    /**
     * 设置文本内容
     * @param {string} text 文本内容
     */
    setText(text) {
        this.element.textContent = text;
    }

    /**
     * 获取文本内容
     * @returns {string} 文本内容
     */
    getText() {
        return this.element.textContent;
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

    /**
     * 获取随机字体
     * @returns {string} 随机字体
     */
    getRandomFontFamily() {
        const fonts = ['Arial, sans-serif', 'Verdana, sans-serif', 'Times New Roman, serif', 'Courier New, monospace'];
        return fonts[Math.floor(Math.random() * fonts.length)];
    }

    /**
     * 获取随机文本
     * @returns {string} 随机文本
     */
    getRandomText() {
        const texts = [
            '你好', '世界', '随机', '文本', '示例', '测试', '演示', '你好世界', '示例文本', '测试文本',
            '随机文本', '演示文本', '示例1', '示例2', '示例3', '示例4', '示例5', '测试1', '测试2', '测试3',
            '测试4', '测试5', '演示1', '演示2', '演示3', '演示4', '演示5', '文本1', '文本2', '文本3',
            '文本4', '文本5', '随机1', '随机2', '随机3', '随机4', '随机5', '你好1', '你好2', '你好3',
            '你好4', '你好5', '世界1', '世界2', '世界3', '世界4', '世界5', '示例文本1', '示例文本2', '示例文本3',
            '示例文本4', '示例文本5', '测试文本1', '测试文本2', '测试文本3', '测试文本4', '测试文本5', '演示文本1',
            '演示文本2', '演示文本3', '演示文本4', '演示文本5', '随机文本1', '随机文本2', '随机文本3', '随机文本4',
            '随机文本5', '你好世界1', '你好世界2', '你好世界3', '你好世界4', '你好世界5', '示例1文本', '示例2文本',
            '示例3文本', '示例4文本', '示例5文本', '测试1文本', '测试2文本', '测试3文本', '测试4文本', '测试5文本',
            '演示1文本', '演示2文本', '演示3文本', '演示4文本', '演示5文本', '随机1文本', '随机2文本', '随机3文本',
            '随机4文本', '随机5文本', '你好1世界', '你好2世界', '你好3世界', '你好4世界', '你好5世界'
        ];
        return texts[Math.floor(Math.random() * texts.length)];
    }
}