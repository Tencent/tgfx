/**
 * EmceptionAdapter - 纯真实Emception适配器（无模拟代码）
 * 适配EmceptionCore，提供C++编译到WebAssembly的功能
 */
export default class EmceptionAdapter {
    constructor() {
        this.emceptionModule = null;
        this.isInitialized = false;
        this.compilationCache = new Map();

        // 默认编译选项
        this.defaultCompileOptions = {
            optimization: '-O2',
            target: 'wasm32',
            outputFormat: 'wasm',
            enableExceptions: true
        };
        this.compileOptions = { ...this.defaultCompileOptions };
    }

    /**
     * 初始化Emception编译器
     */
    async initialize(options = {}) {
        if (this.isInitialized) {
            console.log('⚠️ Emception adapter already initialized');
            return true;
        }

        try {
            console.log('🔄 Initializing EmceptionAdapter...');

            // 1. 加载并初始化EmceptionCore
            await this.loadEmceptionCore();

            // 2. 准备TGFX头文件环境
            await this.prepareTGFXEnvironment();

            // 3. 配置编译器选项
            this.configureCompiler(options);

            this.isInitialized = true;
            console.log('✅ EmceptionAdapter initialized successfully');
            return true;

        } catch (error) {
            console.error('❌ EmceptionAdapter initialization failed:', error);
            throw new Error(`EmceptionAdapter initialization failed: ${error.message}`);
        }
    }

    /**
     * 加载EmceptionCore模块并初始化
     */
    async loadEmceptionCore() {
        try {
            console.log('🔄 Loading EmceptionCore...');

            // 动态导入EmceptionCore
            const EmceptionCoreModule = await import('./emception-core.js');

            // 创建并初始化EmceptionCore实例
            this.emceptionModule = new EmceptionCoreModule.default();
            await this.emceptionModule.init();

            console.log('✅ EmceptionCore loaded and initialized successfully');

        } catch (error) {
            console.error('❌ Failed to load EmceptionCore:', error);
            throw new Error(`Failed to load EmceptionCore: ${error.message}`);
        }
    }

    /**
     * 准备TGFX环境（头文件）
     */
    async prepareTGFXEnvironment() {
        console.log('📚 Preparing TGFX environment...');

        try {
            // 首先检查EmceptionCore的文件系统状态
            console.log('🔍 Checking EmceptionCore file system status...');
            console.log('📊 EmceptionCore initialized:', this.emceptionModule.initialized);
            console.log('📊 FileSystem available:', !!this.emceptionModule.fileSystem);

            // 检查根目录是否存在
            const rootExists = this.emceptionModule.exists('/');
            console.log('📊 Root directory exists:', rootExists);

            // 尝试创建基础目录结构
            console.log('📁 Creating base directory structure...');

            // 确保根目录下的tgfx目录存在
            if (!this.emceptionModule.exists('/tgfx')) {
                console.log('📁 Creating /tgfx directory...');
                this.emceptionModule.fileSystem.mkdirTree('/tgfx');
                console.log('✅ /tgfx directory created');
            }

            // 确保include目录存在
            if (!this.emceptionModule.exists('/tgfx/include')) {
                console.log('📁 Creating /tgfx/include directory...');
                this.emceptionModule.fileSystem.mkdirTree('/tgfx/include');
                console.log('✅ /tgfx/include directory created');
            }

            // 确保emscripten子目录存在
            if (!this.emceptionModule.exists('/tgfx/include/emscripten')) {
                console.log('📁 Creating /tgfx/include/emscripten directory...');
                this.emceptionModule.fileSystem.mkdirTree('/tgfx/include/emscripten');
                console.log('✅ /tgfx/include/emscripten directory created');
            }

            // TGFX API头文件（无依赖版本）
            // TGFX API头文件（纯C接口版本，避免C++类的复杂性）
            const tgfxHeaders = {
                'tgfx_c_api.h': `#pragma once

extern "C" {
// 句柄式资源与稳定 C ABI
int  tgfx_CreatePaint();
void tgfx_DisposePaint(int handle);
void tgfx_SetPaintColor(int handle, float r, float g, float b, float a);
void tgfx_SetPaintStyle(int handle, int style /*0=Fill,1=Stroke*/);
void tgfx_SetPaintStrokeWidth(int handle, float width);

void tgfx_ClearCanvas(float r, float g, float b, float a);
void tgfx_DrawRectWithPaint(float left, float top, float right, float bottom, int paintHandle);
void tgfx_DrawCircleWithPaint(float cx, float cy, float radius, int paintHandle);
void tgfx_Present();
}

namespace tgfx {
  struct Color { float r,g,b,a; };
  struct Point { float x,y; };
  struct Rect  { float left, top, right, bottom; 
                  float width()  const { return right-left; }
                  float height() const { return bottom-top; } };
  enum class PaintStyle { Fill = 0, Stroke = 1 };

  class Paint {
  public:
    Paint(): handle_(tgfx_CreatePaint()) {}
    ~Paint(){ if(handle_>0) tgfx_DisposePaint(handle_); }
    Paint(const Paint&) = delete;
    Paint& operator=(const Paint&) = delete;
    Paint(Paint&& other) noexcept : handle_(other.handle_) { other.handle_ = 0; }
    Paint& operator=(Paint&& other) noexcept { if(this!=&other){ if(handle_>0) tgfx_DisposePaint(handle_); handle_=other.handle_; other.handle_=0;} return *this; }

    void setColor(const Color& c){ tgfx_SetPaintColor(handle_, c.r,c.g,c.b,c.a); }
    void setStyle(PaintStyle s){ tgfx_SetPaintStyle(handle_, (int)s); }
    void setStrokeWidth(float w){ tgfx_SetPaintStrokeWidth(handle_, w); }

    int handle() const { return handle_; }
  private:
    int handle_;
  };

  inline Color MakeColor(float r,float g,float b,float a=1.0f){ return {r,g,b,a}; }
  inline Point MakePoint(float x,float y){ return {x,y}; }
  inline Rect  MakeRect(float l,float t,float r,float b){ return {l,t,r,b}; }

  inline void ClearCanvas(const Color& c){ tgfx_ClearCanvas(c.r,c.g,c.b,c.a); }
  inline void DrawRect(const Rect& rc, const Paint& p){ tgfx_DrawRectWithPaint(rc.left,rc.top,rc.right,rc.bottom, p.handle()); }
  inline void DrawCircle(float cx,float cy,float radius, const Paint& p){ tgfx_DrawCircleWithPaint(cx,cy,radius, p.handle()); }
  inline void DrawCircle(const Point& c,float radius, const Paint& p){ tgfx_DrawCircleWithPaint(c.x,c.y,radius, p.handle()); }
  inline void Present(){ tgfx_Present(); }
}

using namespace tgfx;`
            };

            // 写入头文件到EmceptionCore文件系统
            console.log('📝 Writing header files...');
            for (const [filename, content] of Object.entries(tgfxHeaders)) {
                try {
                    const fullPath = `/tgfx/include/${filename}`;

                    console.log(`📝 Writing file: ${fullPath}`);
                    console.log(`📊 File content length: ${content.length} characters`);

                    // 检查目标目录是否存在
                    const dirPath = fullPath.substring(0, fullPath.lastIndexOf('/'));
                    const dirExists = this.emceptionModule.exists(dirPath);
                    console.log(`📊 Target directory ${dirPath} exists: ${dirExists}`);

                    if (!dirExists) {
                        console.log(`📁 Creating directory: ${dirPath}`);
                        this.emceptionModule.fileSystem.mkdirTree(dirPath);
                    }

                    // 写入文件
                    this.emceptionModule.writeFile(fullPath, content);
                    console.log(`✅ Successfully written: ${fullPath}`);

                    // 验证文件是否写入成功
                    const fileExists = this.emceptionModule.exists(fullPath);
                    console.log(`📊 File verification - ${fullPath} exists: ${fileExists}`);

                } catch (fileError) {
                    console.error(`❌ Failed to write file ${filename}:`, fileError);
                    console.error('📊 Error details:', {
                        name: fileError.name,
                        message: fileError.message,
                        code: fileError.code,
                        errno: fileError.errno,
                        stack: fileError.stack
                    });
                    throw fileError;
                }
            }

            console.log('✅ TGFX environment prepared successfully');

        } catch (error) {
            console.error('❌ Failed to prepare TGFX environment:', error);
            console.error('📊 Error details:', {
                name: error.name,
                message: error.message,
                code: error.code,
                errno: error.errno,
                stack: error.stack
            });

            // 尝试诊断文件系统状态
            console.log('🔍 Diagnosing file system state...');
            try {
                console.log('📊 Root directory contents:');
                // 这里可以添加更多诊断信息
            } catch (diagError) {
                console.error('❌ Failed to diagnose file system:', diagError);
            }

            throw new Error(`Failed to prepare TGFX environment: ${error.message}`);
        }
    }

    /**
     * 配置编译器选项
     */
    configureCompiler(options = {}) {
        console.log('⚙️ Configuring compiler options...');

        // 合并用户选项和默认选项
        this.compileOptions = {
            ...this.defaultCompileOptions,
            ...options.compileOptions
        };

        console.log('✅ Compiler configuration complete');
    }

    /**
     * 编译C++代码到WebAssembly
     */
    async compile(sourceCode, options = {}) {
        if (!this.isInitialized) {
            throw new Error('EmceptionAdapter not initialized');
        }

        try {
            console.log('🔨 Starting C++ compilation...');

            // 生成编译缓存ID
            const compileId = this.generateCompileId(sourceCode, options);

            // 检查缓存
            if (this.compilationCache.has(compileId)) {
                console.log('🎯 Using cached compilation result');
                return this.compilationCache.get(compileId);
            }

            // 1. 准备源文件
            const sourceFile = '/working/main.cpp';
            const wrappedCode = this.wrapUserCode(sourceCode);
            this.emceptionModule.writeFile(sourceFile, wrappedCode);
            console.log('📝 Source file written to:', sourceFile);

            // 2. 执行编译
            const compileOptions = {
                ...this.compileOptions,
                ...options
            };

            const result = await this.performCompilation(sourceFile, compileOptions);

            // 3. 缓存结果
            this.compilationCache.set(compileId, result);

            console.log('✅ Compilation completed successfully');
            return result;

        } catch (error) {
            console.error('❌ Compilation failed:', error);
            throw new Error(`Compilation failed: ${error.message}`);
        }
    }

    /**
    /**
     * 包装用户代码 - 支持标准C++语法的Paint类
     */
    wrapUserCode(userCode) {
        // 检查用户代码是否已经包含函数定义
        const hasFunctionDef = /void\s+\w+\s*\([^)]*\)\s*\{/.test(userCode) || /int\s+main\s*\([^)]*\)\s*\{/.test(userCode);

        let processedCode;

        if (hasFunctionDef) {
            // 如果用户代码已经包含函数定义，保持标准C++语法
            processedCode = userCode;

            // 将draw()函数重命名为user_main()，保持main()函数不变
            if (processedCode.includes('void draw(')) {
                processedCode = processedCode.replace(/void\s+draw\s*\(\s*\)\s*\{/, 'extern "C" int user_main() {');
                // 在函数结尾添加return 0;
                processedCode = processedCode.replace(/\}(\s*)$/, '    return 0;\n}$1');
            } else if (processedCode.includes('int main(')) {
                // 将main函数重命名为user_main并添加导出标记
                processedCode = processedCode.replace(/int\s+main\s*\([^)]*\)\s*\{/, 'extern "C" int user_main() {');
            }

        } else {
            // 如果用户代码只是代码片段，包装成函数，保持标准C++语法
            processedCode = userCode;
        }

        console.log('🔄 代码转换前:', userCode);
        console.log('🔄 代码转换后:', processedCode);

        // 使用标准的TGFX API头文件，让用户写标准C++代码
            const finalCode = hasFunctionDef ?
            `#include "tgfx_c_api.h"\n\n${processedCode}` :
            `#include "tgfx_c_api.h"\n\nextern "C" {\n    int user_main() {\n        ${processedCode}\n        return 0;\n    }\n}`;

        console.log('🔄 最终包装代码:', finalCode);
        return finalCode;
    }

    /**
     * 执行编译操作
     */
    async performCompilation(sourceFile, options) {
        const objectFile = '/working/temp.o';
        const outputFile = '/working/output.wasm';

        console.log('🔧 Using two-step compilation process...');

        console.log("-------"+sourceFile);
        // 步骤1：编译到目标文件
        const compileCommand = [
            'clang++',
            '-target', 'wasm32',
            '-O2','-fno-exceptions','-fno-rtti',
            '-c',  // 只编译，不链接
            `-I/tgfx/include`,  // 包含头文件路径
            sourceFile,
            '-o',
            objectFile
        ].join(' ');

        console.log('🔧 Step 1 - Compile to object:', compileCommand);

        let compileResult;
        try {
            compileResult = await this.emceptionModule.run(compileCommand);
            console.log('✅ Compilation completed, returncode:', compileResult.returncode);
        } catch (error) {
            console.error('❌ Compilation failed:', error);
            throw new Error(`Compilation failed: ${error.message}`);
        }

        if (compileResult.returncode !== 0) {
            throw new Error(`Compilation failed with return code ${compileResult.returncode}: ${compileResult.stderr || 'Unknown error'}`);
        }

        // 步骤2：使用LLD链接到WASM
        const linkCommand = [
            'lld',
            '-flavor', 'wasm',
            '--no-entry',             // 不需要 main 入口
            '--export=user_main',     // 只导出用户入口
            '--allow-undefined',
            objectFile,
            '-o',
            outputFile
        ].join(' ');

        console.log('🔧 Step 2 - Link with LLD:', linkCommand);

        let linkResult;
        try {
            linkResult = await this.emceptionModule.run(linkCommand);
            console.log('✅ Linking completed, returncode:', linkResult.returncode);
        } catch (error) {
            console.error('❌ Linking failed:', error);
            throw new Error(`Linking failed: ${error.message}`);
        }

        if (linkResult.returncode !== 0) {
            throw new Error(`Linking failed with return code ${linkResult.returncode}: ${linkResult.stderr || 'Unknown error'}`);
        }

        // 读取生成的WASM文件
        let wasmBinary;
        try {
            wasmBinary = this.emceptionModule.readFile(outputFile);
            console.log('✅ WASM binary read successfully, size:', wasmBinary.length);

            // 验证WASM文件格式
            if (wasmBinary.length >= 4 &&
                wasmBinary[0] === 0x00 && wasmBinary[1] === 0x61 &&
                wasmBinary[2] === 0x73 && wasmBinary[3] === 0x6D) {
                console.log('✅ WASM file format validated!');
            } else {
                console.warn('⚠️ WASM file format validation failed');
            }
        } catch (error) {
            console.error('❌ Failed to read WASM file:', error);
            throw new Error(`Failed to read output WASM file: ${error.message}`);
        }

        return {
            success: true,
            wasmBinary,
            exports: ['user_main'],
            stderr: (compileResult.stderr || '') + (linkResult.stderr || ''),
            stdout: (compileResult.stdout || '') + (linkResult.stdout || '')
        };
    }

    /**
     * 生成编译缓存ID
     */
    generateCompileId(sourceCode, options) {
        const content = sourceCode + JSON.stringify(options);

        // 使用简单哈希避免编码问题
        let hash = 0;
        for (let i = 0; i < content.length; i++) {
            const char = content.charCodeAt(i);
            hash = ((hash << 5) - hash) + char;
            hash = hash & hash; // 转换为32位整数
        }
        return Math.abs(hash).toString(16).substring(0, 16);
    }

    /**
     * 清除编译缓存
     */
    clearCache() {
        this.compilationCache.clear();
        console.log('🗑️ Compilation cache cleared');
    }

    /**
     * 获取编译器信息
     */
    getCompilerInfo() {
        if (!this.isInitialized) {
            return null;
        }

        return {
            type: 'EmceptionCore',
            initialized: this.isInitialized,
            options: this.compileOptions,
            cacheSize: this.compilationCache.size
        };
    }
}
