import ElementManager from './ElementManager.js';
import BackendManager from './BackendManager.js';

export default class PerformanceManager {
    private readonly fpsCounter: HTMLElement | null;
    private readonly renderCounter: HTMLElement | null;
    private readonly shapeCounter: HTMLElement | null;
    private lastFrameTime: number;
    private frameCount: number;
    private elementManager: ElementManager;
    private backendManager: BackendManager;

    constructor(elementManager: ElementManager, backendManager: BackendManager) {
        this.fpsCounter = document.getElementById('fpsCounter');
        this.renderCounter = document.getElementById('renderCounter');
        this.shapeCounter = document.getElementById('shapeCounter') as HTMLElement;

        this.lastFrameTime = performance.now();
        this.frameCount = 0;
        this.elementManager = elementManager;
        this.backendManager = backendManager;
        this.updatePerformanceInfo = this.updatePerformanceInfo.bind(this);
        requestAnimationFrame(this.updatePerformanceInfo);
    }


    private updatePerformanceInfo(): void {
        const fps = this.calculateFPS();
        if (fps > 0) {
            this.updateUI(fps);
        }

        requestAnimationFrame(this.updatePerformanceInfo);
    }

    private calculateFPS(): number {
        const now = performance.now();
        this.frameCount++;
        const delta = now - this.lastFrameTime;
        if (delta >= 1000) {
            const fps = (this.frameCount / delta) * 1000;
            this.frameCount = 0;
            this.lastFrameTime = now;
            return Math.round(fps);
        }
        return 0;
    }

    private updateUI(fps: number): void {
        if (this.fpsCounter) {
            this.fpsCounter.textContent = `FPS: ${fps}`;
        }

        if (this.shapeCounter) {
            const count = this.elementManager.getElements().length;
            this.shapeCounter.textContent = `图形数量: ${count}`;
        }

        if (this.renderCounter) {
            const frameTime = this.backendManager.frameTimeCons();
            this.renderCounter.textContent = `单帧耗时: ${frameTime.toFixed(2)} ms`;
        }
    }

}