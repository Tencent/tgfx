/**
 * WASM执行器 - 运行编译后的WebAssembly代码并与TGFX渲染系统集成
 */

class WasmExecutor {
    constructor() {
        this.wasmModule = null;
        this.wasmExports = null;
        this.wasmMemory = null;
        this.isReady = false;
    }

    /**
     * 加载 Emscripten 生成的单文件 JS 运行时模块
     */
    async loadJSRuntime(jsModuleCode, wasmBinary) {
        try {
            // 预下载资源
            await this.predownloadImageData();
            const svgBytes = await this.predownloadSvgData();
            if (svgBytes) window.preloadedSvgData = svgBytes;

            // 创建模块
            const blob = new Blob([jsModuleCode], { type: 'application/javascript' });
            const url = URL.createObjectURL(blob);
            const modFactory = (await import(/* webpackIgnore: true */ url)).default;
            URL.revokeObjectURL(url);

            const factoryOpts = {};
            if (wasmBinary?.buffer) {
                factoryOpts.wasmBinary = wasmBinary;
                factoryOpts.locateFile = (p) => p;
            }

            factoryOpts.preRun = [(Module) => this.preloadImageAssetsSync(Module)];

            this.wasmModule = await modFactory(factoryOpts);
            this.wasmExports = this.wasmModule;
            this.wasmMemory = this.wasmModule.HEAPU8?.buffer ? { buffer: this.wasmModule.HEAPU8.buffer } : null;
            this.isReady = true;

            console.log('JS runtime module loaded');
            return true;
        } catch (error) {
            console.error('❌ JS runtime load failed:', error);
            throw new Error(`JS运行时加载失败: ${error.message}`);
        }
    }

    /**
     * 同步预加载资源到运行时文件系统
     */
    preloadImageAssetsSync(Module) {
        if (!Module.FS) return;

        const FS = Module.FS;

        // 创建目录
        ['/working', '/working/assets'].forEach(dir => {
            try { FS.mkdir(dir); } catch (e) {
                if (e.errno !== 20) console.log(`创建${dir}目录失败:`, e.errno);
            }
        });

        // 写入文件
        const files = [
            { data: window.preloadedImageData, path: '/working/assets/image.webp', type: '图片' },
            { data: window.preloadedSvgData, path: '/working/assets/sample.svg', type: 'SVG' }
        ];

        files.forEach(({ data, path, type }) => {
            if (data) {
                try {
                    FS.writeFile(path, data);
                    console.log(`${type}文件写入成功，大小:`, data.length, "bytes");
                } catch (e) {
                    console.log(`${type}文件写入失败:`, e.errno, e.message);
                }
            } else {
                console.log(`没有预加载的${type}数据`);
            }
        });
    }

    /**
     * 异步预下载图片数据
     */
    async predownloadImageData() {
        try {
            const response = await fetch('../resources/apitest/imageReplacement.webp');
            if (response.ok) {
                window.preloadedImageData = new Uint8Array(await response.arrayBuffer());
            }
        } catch (error) {
            // 静默处理
        }
    }

    /**
     * 异步预下载SVG数据
     */
    async predownloadSvgData() {
        const paths = ["../resources/apitest/SVG/path.svg", "../resources/apitest/SVG/text.svg", "../resources/apitest/SVG/blur.svg"];

        for (const path of paths) {
            try {
                const res = await fetch(path);
                if (res?.ok) {
                    const buf = await res.arrayBuffer();
                    console.log("成功加载SVG:", path, "大小:", buf.byteLength, "bytes");
                    return new Uint8Array(buf);
                }
            } catch (e) {
                console.log("SVG加载异常:", path, e.message);
            }
        }
        return null;
    }

    /**
     * 执行主函数
     */
    async execute() {
        if (!this.isReady) throw new Error('WASM模块未准备就绪');

        const fn = this.wasmModule?.cwrap ? this.wasmModule.cwrap('main', 'number', []) :
                   this.wasmExports?.main || this.wasmExports?._main;

        if (!fn) throw new Error('未找到main函数');

        const result = fn();
        console.log(`WASM执行完成，返回值: ${result}`);
        return result;
    }

    /**
     * 清理资源
     */
    cleanup() {
        this.wasmModule = null;
        this.wasmExports = null;
        this.wasmMemory = null;
        this.isReady = false;
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
