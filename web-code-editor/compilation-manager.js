/**
 * CompilationManager - 管理编译 Worker 的主线程接口
 */

export default class CompilationManager {
    constructor() {
        this.worker = null;
        this.isInitialized = false;
        this.messageId = 0;
        this.pendingMessages = new Map();
        this.currentCompilation = null;
    }

    async initialize(options = {}) {
        if (this.isInitialized) return;

        // 创建 Worker
        this.worker = new Worker('./compilation-worker.js', { type: 'module' });

        // 监听 Worker 消息
        this.worker.addEventListener('message', (event) => {
            this.handleWorkerMessage(event.data);
        });

        // 初始化 Worker 中的适配器
        await this.sendMessage('init', options);
        this.isInitialized = true;
    }

    async compile(sourceCode, options = {}) {
        if (!this.isInitialized) {
            throw new Error('CompilationManager not initialized');
        }

        // 取消当前编译
        if (this.currentCompilation) {
            this.currentCompilation.abort();
        }

        // 创建新的编译任务
        this.currentCompilation = this.sendMessage('compile', { sourceCode, options });

        try {
            const result = await this.currentCompilation;
            this.currentCompilation = null;
            return result;
        } catch (error) {
            this.currentCompilation = null;
            throw error;
        }
    }

    async compileAndExecute(sourceCode, options = {}) {
        // 编译
        const compileResult = await this.compile(sourceCode, options);

        if (!compileResult.success) {
            throw new Error('Compilation failed');
        }

        // 执行编译结果
        return await this.executeCompiledCode(compileResult);
    }

    async executeCompiledCode(compileResult) {
        const { jsModuleCode, wasmBinary } = compileResult;

        // 使用 WasmExecutor 来执行编译结果
        const { WasmExecutor } = await import('./wasm-executor.js');
        const executor = new WasmExecutor();

        try {
            // 使用 WasmExecutor 加载并执行 JS 运行时
            await executor.loadJSRuntime(jsModuleCode, wasmBinary);

            // 执行 main 函数
            const result = await executor.execute('main');

            return {
                success: true,
                result: result,
                executor: executor
            };

        } catch (error) {
            console.error('执行失败:', error);
            executor.cleanup();
            throw error;
        }
    }

    async clearCache() {
        if (!this.isInitialized) return;
        await this.sendMessage('clearCache');
    }

    terminate() {
        if (this.worker) {
            this.worker.terminate();
            this.worker = null;
            this.isInitialized = false;
        }
    }

    // 私有方法
    sendMessage(type, data = {}) {
        return new Promise((resolve, reject) => {
            const id = ++this.messageId;

            // 存储 Promise 回调
            this.pendingMessages.set(id, { resolve, reject });

            // 发送消息到 Worker
            this.worker.postMessage({ id, type, data });

            // 设置超时
            setTimeout(() => {
                if (this.pendingMessages.has(id)) {
                    this.pendingMessages.delete(id);
                    reject(new Error(`Message timeout: ${type}`));
                }
            }, 60000); // 60秒超时
        });
    }

    handleWorkerMessage(message) {
        const { id, success, result, error } = message;

        if (!this.pendingMessages.has(id)) return;

        const { resolve, reject } = this.pendingMessages.get(id);
        this.pendingMessages.delete(id);

        if (success) {
            resolve(result);
        } else {
            reject(new Error(error));
        }
    }
}
