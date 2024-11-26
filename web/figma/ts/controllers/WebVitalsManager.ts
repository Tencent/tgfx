import {onINP, resetINP} from '../controllers/onINP.js';
import {Metric} from "web-vitals";

/**
 * WebVitalsManager 类用于管理 web-vitals 的指标回调，特别是 INP 指标。
 */
export default class WebVitalsManager {
    private readonly inpCallback: (metric: Metric) => void;

    /**
     * 构造函数
     * @param callback 处理 INP 指标的回调函数
     */
    constructor(callback: (metric: Metric) => void) {
        this.inpCallback = callback;
        this.init();
    }

    private init() {
        onINP((metric) => {
            console.log(metric);
            this.inpCallback(metric);
        }, {reportAllChanges: true, durationThreshold: 0});
    }

    /**
     * 重置 INP 指标的处理
     * 通过停止当前的回调处理，并允许重新启动
     */
    public reset() {
        resetINP();
    }
}