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
// js/models/TextElement.ts

import BaseElement from './BaseElement.js';

export default class TextElement extends BaseElement {
    /**
     * 创建文本元素
     */
    constructor() {
        super('text');
        // 默认属性
        this.setAttribute('x', this.getRandomInt(0, 1000).toString());
        this.setAttribute('y', this.getRandomInt(0, 800).toString());
        this.setAttribute('text-anchor', "start");
        this.setAttribute('dominant-baseline', "text-before-edge");
        this.setAttribute('fill', this.getRandomColor());
        this.setAttribute('font-size', this.getRandomInt(12, 36).toString());
        this.setAttribute('font-family', this.getRandomFontFamily());
        this.element.textContent = this.getRandomText();
    }

    /**
     * 设置文本内容
     * @param text - 文本内容
     */
    setText(text: string): void {
        this.element.textContent = text;
    }

    /**
     * 获取文本内容
     * @returns 文本内容
     */
    getText(): string | null {
        return this.element.textContent;
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

    /**
     * 获取随机字体
     * @returns 随机字体
     */
    getRandomFontFamily(): string {
        const fonts = ['Arial, sans-serif', 'Verdana, sans-serif', 'Times New Roman, serif', 'Courier New, monospace'];
        return fonts[Math.floor(Math.random() * fonts.length)];
    }

    /**
     * 获取随机文本
     * @returns 随机文本
     */
    getRandomText(): string {
        const texts = [
            "他是苏军",
            "他是盟军",
            "占了我的油田",
            "牛车采了我的矿",
            "他是蛋蛋色（一生之敌）",
            "他是我邻居",
            "他重工出的比较慢",
            "他坦克（步兵）比较多对我有威胁",
            "他作战单位比较少是个经验宝宝",
            "他比较会玩",
            "他不会玩是个萌新宝宝",
            "他发展的比较好钱多",
            "他发展的不好卡钱了",
            "我的钱很多留着也带不出去不如多造点坦克",
            "我有点卡钱要先下手为强",
            "地图大要打发展",
            "地图小要打快攻",
            "他延伸兵营（地堡，电厂）想和我抢地盘",
            "他电厂（兵营）和我挨的比较进",
            "他地堡放我家里（其实往往是在人家家里）",
            "我这个位置比较好延伸抢地盘",
            "他是韩国想闭关玩飞机",
            "他是法国造了巨炮（或者一开始就被抢死）",
            "他是英国不停的用狙击手点我的兵",
            "他是利比亚造了个自爆卡车玩火自焚",
            "他房间名叫××王11进去和他玩玩（有一种傲慢之罪）",
            "这是个新图我们进去玩玩",
            "现在形成了三国局面",
            "这个人开局点了f10叫嚣",
            "这个人龟缩在家里不出来",
            "他的牛比我多一点",
            "他的牛出的比较慢",
            "他开局睡着了基地展开比较慢",
            "他造了一个××比较嚣张",
            "他俩在打架，我们得去劝架",
            "我们捡了一个三星火力防空车",
            "他造了一个偷车想要偷我们",
            "他想用坦克冲我",
            "他们打了个两败俱伤我去肥手套",
            "××回手掏我的基地",
            "他想和我换家",
            "他卖基地想冲我",
            "我卖掉基地显得专业一些",
            "他干掉了××，我们去帮××报仇",
            "他的蜘蛛上了我的牛",
            "我的蜘蛛“滋溜”一声上了他的牛",
            "他打字给××吸引仇恨",
            "他们准备孙刘联盟",
            "他造了一个黑幕我们看不见",
            "他准备搞养殖业",
            "他的大兵一直A地面太吵了",
            "我的队友是“卧龙凤雏”对吧那就只能痛下杀手了",
            "他没有招惹我，也对我没有威胁，但我就是想打他",
            "他的xx一直在打地面，非常的吵（强制攻击地面的单位再次选中按下s即可停止）",
            "我看他不顺眼（终于承认了）",
            "他有一点慌，基地动了一下",
            "好今天给大家带来一局每人一小块地/冰天混战",
            "这个小（），他基地展开满了1，2秒钟，那，他就是个s人了对不对",
            "结果这个时候你们看，这个小（），他把地堡放到我们家里，这是几个意思嘞",
            "这个小（），你怎么分不清形势嘞，你邻居都没带走就来打我",
            "小车子拐进来，唉，刺溜，进去，我们这个偷，三个地堡都守不住。/钻了他的牛车",
            "我想造一个高科技，但是现在捏，我们十分卡钱，连（）都出不起",
            "我们就掐住他的头，左右摇摆，唉",
            "这下不好打了啊，我的队友被带走了",
            "然后他就受不了了啊，直接（拔线）投降"
        ];
        return texts[Math.floor(Math.random() * texts.length)];
    }
}