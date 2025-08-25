/**
 * WASM执行器 - 运行编译后的WebAssembly代码并与TGFX渲染系统集成
 * 支持真实WASM执行
 */

class WasmExecutor {
    constructor() {
        this.wasmModule = null;
        this.wasmExports = null;
        this.wasmMemory = null;
        this.isReady = false;

    }


    /**
     * 加载 Emscripten 生成的单文件 JS 运行时模块（.mjs 字符串）
     */
    async loadJSRuntime(jsModuleCode, wasmBinary) {
        try {
            // 首先预下载图片数据
            await this.predownloadImageData();

            const blob = new Blob([jsModuleCode], { type: 'application/javascript' });
            const url = URL.createObjectURL(blob);
            const modFactory = (await import(/* webpackIgnore: true */ url)).default;
            URL.revokeObjectURL(url);

            // 创建 Module 实例
            const factoryOpts = {};
            if (wasmBinary && wasmBinary.buffer) {
                factoryOpts.wasmBinary = wasmBinary;
                // 避免模块尝试拼接 wasm URL
                factoryOpts.locateFile = (p) => p;
            }

            // 添加 preRun 回调来预加载图片资源
            factoryOpts.preRun = [(Module) => {
                this.preloadImageAssetsSync(Module);
            }];

            this.wasmModule = await modFactory(factoryOpts);
            this.wasmExports = this.wasmModule; // em++ 单文件将 cwrap/HEAPU8 暴露在 module 上
            this.wasmMemory = this.wasmModule.HEAPU8?.buffer ? { buffer: this.wasmModule.HEAPU8.buffer } : null;
            this.isReady = true;

            console.log('✅ JS runtime module loaded');
            return true;
        } catch (error) {
            console.error('❌ JS runtime load failed:', error);
            throw new Error(`JS运行时加载失败: ${error.message}`);
        }
    }

    /**
     * 同步预加载图片资源到运行时文件系统
     */
    preloadImageAssetsSync(Module) {
        try {
            if (!Module.FS) return;

            const FS = Module.FS;
            
            // 确保目录存在
            try { FS.mkdir('/working'); } catch (e) {}
            try { FS.mkdir('/working/assets'); } catch (e) {}

            // 写入预下载的图片数据
            if (window.preloadedImageData) {
                FS.writeFile('/working/assets/image.webp', window.preloadedImageData);
            }
        } catch (error) {
            console.error('图片资源预加载失败:', error);
        }
    }

    /**
     * 异步预下载图片数据
     */
    async predownloadImageData() {
        try {
            const imageUrl = '/resources/apitest/imageReplacement.webp';
            const response = await fetch(imageUrl);
            if (!response.ok) {
                throw new Error(`下载失败: ${response.status} ${response.statusText}`);
            }
            
            const arrayBuffer = await response.arrayBuffer();
            window.preloadedImageData = new Uint8Array(arrayBuffer);
        } catch (error) {
            console.error('图片数据预下载失败:', error);
        }
    }

    /**
     * 加载并实例化WebAssembly模块
     */
    async loadWasm(wasmBinary, imports = {}) {
        try {
            console.log('正在加载WebAssembly模块...');
            if (imports && imports.env) {
                console.log('传入的导入对象(env keys):', Object.keys(imports.env));
            }

            // 实例化WebAssembly模块
            const wasmModule = await WebAssembly.instantiate(wasmBinary, imports);

            this.wasmModule = wasmModule;
            this.wasmExports = wasmModule.instance.exports;
            this.wasmMemory = this.wasmExports.memory || imports?.env?.memory || null;
            this.isReady = true;

            console.log('✅ WebAssembly模块加载成功');
            console.log('可用导出:', Object.keys(this.wasmExports));

            return true;

        } catch (error) {
            console.error('❌ WebAssembly加载失败:', error);
            console.error('❌ 错误详情:', error.message);
            console.error('❌ 错误堆栈:', error.stack);
            throw new Error(`WASM加载失败: ${error.message}`);
        }
    }



    /**
     * 执行用户主函数
     */
    async execute(functionName = 'user_main') {
        if (!this.isReady) {
            throw new Error('WASM模块未准备就绪');
        }

        try {
            const candidates = [functionName, functionName === 'user_main' ? 'main' : 'user_main'];
            const hasCwrap = typeof this.wasmModule?.cwrap === 'function';

            if (hasCwrap) {
                for (const name of candidates) {
                    try {
                        const fn = this.wasmModule.cwrap(name, 'number', []);
                        if (typeof fn === 'function') {
                            const result = fn();
                            console.log(`✅ WASM执行完成，入口: ${name} 返回值: ${result}`);
                            return result;
                        }
                    } catch {
                        // 尝试下一个入口
                    }
                }
                throw new Error(`未找到可执行入口: ${candidates.join(' / ')}`);
            } else {
                for (const name of candidates) {
                    const fn = this.wasmExports[name] || this.wasmExports['_' + name];
                    if (typeof fn === 'function') {
                        const result = fn();
                        console.log(`✅ WASM执行完成，入口: ${name} 返回值: ${result}`);
                        return result;
                    }
                }
                throw new Error(`未找到可执行入口: ${candidates.join(' / ')}`);
            }

        } catch (error) {
            console.error('❌ WASM执行失败:', error);
            throw new Error(`WASM执行失败: ${error.message}`);
        }
    }

    /**
     * 清理资源
     */
    cleanup() {
        this.wasmModule = null;
        this.wasmExports = null;
        this.wasmMemory = null;
        this.isReady = false;
        console.log('WASM执行器已清理');
    }

    /**
     * 获取执行器状态
     */
    getStatus() {
        return {
            ready: this.isReady,
            hasModule: !!this.wasmModule,
            hasExports: !!this.wasmExports,
            memorySize: this.wasmMemory ? this.wasmMemory.buffer.byteLength : 0
        };
    }
}

export { WasmExecutor };
