/**
 * 编译管理器 - 整合Emception编译器和WASM执行器
 *
 * 提供完整的C++编译和执行流程管理
 */

import EmceptionAdapter from './emception-adapter.js';
import { WasmExecutor } from './wasm-executor.js';

class CompilationManager {
    constructor(tgfxAPI) {
        this.tgfxAPI = tgfxAPI;
        this.emceptionAdapter = new EmceptionAdapter();
        this.wasmExecutor = new WasmExecutor(tgfxAPI);

        this.isInitialized = false;
        this.compilationHistory = [];
        this.currentSession = null;

        // 编译状态
        this.status = {
            phase: 'idle', // idle, initializing, compiling, executing, error
            progress: 0,
            message: '',
            error: null
        };

        // 事件监听器
        this.eventListeners = new Map();
    }

    /**
     * 初始化编译管理器
     */
    async initialize(options = {}) {
        if (this.isInitialized) {
            return true;
        }

        try {
            this.updateStatus('initializing', 0, '正在初始化编译环境...');
            this.emit('initStart');

            // 初始化Emception适配器
            this.updateStatus('initializing', 30, '正在加载Emception编译器...');
            await this.emceptionAdapter.initialize({
                emceptionPath: options.emceptionPath,
                compileOptions: options.compileOptions
            });

            this.updateStatus('initializing', 100, '编译环境初始化完成');
            this.isInitialized = true;
            this.emit('initComplete');

            console.log('✅ 编译管理器初始化成功');
            return true;

        } catch (error) {
            this.updateStatus('error', 0, '初始化失败', error);
            this.emit('initError', error);
            throw error;
        }
    }

    /**
     * 编译并执行C++代码
     */
    async compileAndExecute(sourceCode, options = {}) {
        if (!this.isInitialized) {
            throw new Error('编译管理器未初始化');
        }

        const sessionId = this.generateSessionId();
        this.currentSession = sessionId;

        try {
            // 第1阶段：编译
            this.updateStatus('compiling', 0, '正在编译C++代码...');
            this.emit('compileStart', { sessionId, sourceCode });

            const compileResult = await this.emceptionAdapter.compile(sourceCode, options);

            this.updateStatus('compiling', 50, '编译完成，准备执行...');
            this.emit('compileComplete', { sessionId, result: compileResult });

            // 第2阶段：加载WASM
            this.updateStatus('executing', 60, '正在加载WebAssembly模块...');

            // 确保TGFX API已经初始化
            if (!this.tgfxAPI || typeof this.tgfxAPI.MakeColor !== 'function') {
                throw new Error('TGFX API未正确初始化，请先初始化TGFX');
            }

            console.log('🔧 TGFX API状态检查:', {
                hasAPI: !!this.tgfxAPI,
                hasMakeColor: typeof this.tgfxAPI.MakeColor,
                hasPaint: typeof this.tgfxAPI.Paint,
                hasDrawRect: typeof this.tgfxAPI.DrawRect
            });

            // 准备WASM导入对象，提供TGFX API函数
            const wasmImports = this.prepareTGFXImports(options.wasmImports);

            await this.wasmExecutor.loadWasm(compileResult.wasmBinary, wasmImports);

            // 建立内存访问连接
            this.wasmExecutor = this.wasmExecutor; // 确保可以访问内存

            // 第3阶段：执行
            this.updateStatus('executing', 80, '正在执行用户代码...');
            this.emit('executeStart', { sessionId });

            const executeResult = await this.wasmExecutor.execute(options.entryFunction);

            // 完成
            this.updateStatus('idle', 100, '执行完成');
            this.emit('executeComplete', { sessionId, result: executeResult });

            // 记录编译历史
            this.recordCompilation(sessionId, sourceCode, compileResult, executeResult);

            return {
                sessionId,
                compileResult,
                executeResult,
                success: true
            };

        } catch (error) {
            this.updateStatus('error', 0, '编译或执行失败', error);
            this.emit('error', { sessionId, error });
            throw error;
        }
    }

    /**
     * 准备TGFX API导入对象 - 使用Embind模式
     */
    prepareTGFXImports(customImports = {}) {
        console.log('🔧 使用稳定 C ABI 导入模式');
        return this.prepareCImports(customImports);
    }

    /**
     * 准备Embind模式的导入对象
     * 让Emscripten自动处理大部分绑定，只提供必要的运行时支持
     */
    prepareCImports(customImports = {}) {
        const TGFX = this.tgfxAPI;

        const handleMap = new Map();
        let nextHandle = 1;
        const allocHandle = (obj) => { const h = nextHandle++; handleMap.set(h, obj); return h; };
        const getHandle = (h) => handleMap.get(h);
        const disposeHandle = (h) => handleMap.delete(h);

        const imports = {
            env: {
                memory: new WebAssembly.Memory({ initial: 256, maximum: 1024 }),

                // Paint 句柄 API
                tgfx_CreatePaint: () => allocHandle(new TGFX.Paint()),
                tgfx_DisposePaint: (h) => disposeHandle(h),
                tgfx_SetPaintColor: (h,r,g,b,a) => { const p=getHandle(h); if(p) p.setColor(TGFX.MakeColor(r,g,b,a)); },
                tgfx_SetPaintStyle: (h,style) => { const p=getHandle(h); if(p) p.setStyle(style===0?TGFX.PaintStyle.Fill:TGFX.PaintStyle.Stroke); },
                tgfx_SetPaintStrokeWidth: (h,w) => { const p=getHandle(h); if(p) p.setStrokeWidth(w); },

                // 绘制 API（使用句柄）
                tgfx_ClearCanvas: (r,g,b,a) => TGFX.ClearCanvas(TGFX.MakeColor(r,g,b,a)),
                tgfx_DrawRectWithPaint: (l,t,rgt,btm, hPaint) => {
                    const p=getHandle(hPaint); if(!p) return; TGFX.DrawRect(TGFX.MakeRect(l,t,rgt,btm), p);
                },
                tgfx_DrawCircleWithPaint: (cx,cy,rad, hPaint) => {
                    const p=getHandle(hPaint); if(!p) return; if(TGFX.DrawCircleXY) TGFX.DrawCircleXY(cx,cy,rad,p); else TGFX.DrawCircle(TGFX.MakePoint(cx,cy),rad,p);
                },
                tgfx_Present: () => TGFX.Present(),

                // 基础桩
                abort: () => { throw new Error('abort'); },
                printf: () => 0,
                puts: () => 0
            }
        };

        if (customImports?.env) Object.assign(imports.env, customImports.env);
        return imports;
    }

    /**
     * 更新状态
     */
    updateStatus(phase, progress, message, error = null) {
        this.status = {
            phase,
            progress: Math.max(0, Math.min(100, progress)),
            message,
            error
        };

        this.emit('statusUpdate', this.status);
    }

    /**
     * 记录编译历史
     */
    recordCompilation(sessionId, sourceCode, compileResult, executeResult) {
        this.compilationHistory.push({
            sessionId,
            timestamp: new Date(),
            sourceCode: sourceCode.substring(0, 200) + '...', // 截取前200字符
            success: compileResult.success,
            executionTime: executeResult?.executionTime || 0
        });

        // 保持历史记录不超过50条
        if (this.compilationHistory.length > 50) {
            this.compilationHistory.shift();
        }
    }

    /**
     * 生成会话ID
     */
    generateSessionId() {
        return `session_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
    }

    /**
     * 事件发射器
     */
    emit(eventName, data) {
        const listeners = this.eventListeners.get(eventName) || [];
        listeners.forEach(callback => {
            try {
                callback(data);
            } catch (error) {
                console.error(`事件监听器错误 (${eventName}):`, error);
            }
        });
    }

    /**
     * 添加事件监听器
     */
    on(eventName, callback) {
        if (!this.eventListeners.has(eventName)) {
            this.eventListeners.set(eventName, []);
        }
        this.eventListeners.get(eventName).push(callback);
    }

    /**
     * 移除事件监听器
     */
    off(eventName, callback) {
        const listeners = this.eventListeners.get(eventName) || [];
        const index = listeners.indexOf(callback);
        if (index > -1) {
            listeners.splice(index, 1);
        }
    }

    /**
     * 清理资源
     */
    cleanup() {
        this.wasmExecutor.cleanup();
        this.emceptionAdapter.clearCache();
        this.eventListeners.clear();
        this.compilationHistory = [];
        this.isInitialized = false;

        console.log('🧹 编译管理器已清理');
    }

    /**
     * 获取API模式信息
     */
    getAPIMode() {
        return {
            mode: 'embind',
            description: 'Embind模式 - 直接对象传递，性能更好',
            features: {
                directObjectPassing: true,
                typeChecking: true,
                memoryManagement: 'automatic',
                performance: 'optimized'
            }
        };
    }

    /**
     * 获取管理器状态
     */
    getManagerStatus() {
        return {
            initialized: this.isInitialized,
            currentStatus: this.status,
            apiMode: this.getAPIMode(),
            emceptionStatus: this.emceptionAdapter.getCompilerInfo(),
            wasmStatus: this.wasmExecutor.getStatus(),
            historyCount: this.compilationHistory.length,
            currentSession: this.currentSession,
            embindSupport: true
        };
    }

    /**
     * 获取编译历史
     */
    getCompilationHistory() {
        return [...this.compilationHistory];
    }

    /**
     * 清除编译历史
     */
    clearHistory() {
        this.compilationHistory = [];
        console.log('🗑️  编译历史已清除');
    }
}

export default CompilationManager;
