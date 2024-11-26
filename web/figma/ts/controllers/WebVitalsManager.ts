import {onINP, Metric} from 'web-vitals';

/**
 * WebVitalsHandler 类用于封装 onINP 的逻辑
 */
class WebVitalsHandler {
    private inpCallback: (metric: Metric) => void;
    private active: boolean = true;
    private id: string;

    constructor(callback: (metric: Metric) => void) {
        this.inpCallback = callback;
        this.id = `${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
        this.init();
    }

    private init() {
        onINP((metric) => {
            console.log(`ID: ${this.id}, this.active: ${this.active}, INP: ${metric.value} ms`);
            if (this.active) {
                this.inpCallback(metric);
            }
        }, {reportAllChanges: true, durationThreshold: 0});
    }

    public destroy() {
        this.active = false;
    }
}

/**
 * WebVitalsManager 类用于管理 web-vitals 的指标回调，特别是 INP 指标。
 */
export default class WebVitalsManager {
    private readonly inpCallback: (metric: Metric) => void;
    private vitalsHandler: WebVitalsHandler;

    /**
     * 构造函数
     * @param callback 处理 INP 指标的回调函数
     */
    constructor(callback: (metric: Metric) => void) {
        this.inpCallback = callback;
        this.vitalsHandler = new WebVitalsHandler(this.inpCallback);
        this.init();
    }

    /**
     * 初始化 INP 指标的监听
     */
    private init() {
        onINP((metric) => {
            this.inpCallback(metric);
        }, {reportAllChanges: true, durationThreshold: 0});
    }

    /**
     * 重置 INP 指标的处理
     * 通过停止当前的回调处理，并允许重新启动
     */
    public reset() {
        if (this.vitalsHandler) {
            this.vitalsHandler.destroy();
        }
        this.vitalsHandler = new WebVitalsHandler(this.inpCallback);
    }
}