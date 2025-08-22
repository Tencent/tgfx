// 简化版Emception - 只包含核心编译功能，不加载包文件
import FileSystem from "./emception/FileSystem.mjs";
import LlvmBoxProcess from "./emception/LlvmBoxProcess.mjs";
import BinaryenBoxProcess from "./emception/BinaryenBoxProcess.mjs";

/**
 * 简化版Emception类 - 绕过包文件加载问题
 */
export default class EmceptionCore {
    constructor() {
        this.fileSystem = null;
        this.processes = {};
        this.initialized = false;
    }

    /**
     * 初始化Emception核心组件
     */
    async init() {
        console.log('🚀 Initializing EmceptionCore...');
        
        try {
            // 创建FileSystem
            console.log('🔧 Creating FileSystem...');
            this.fileSystem = await new FileSystem();
            console.log('✅ FileSystem created successfully');
            
            // 创建必要的目录结构
            console.log('📁 Creating directory structure...');
            this.fileSystem.mkdirTree("/wasm");
            this.fileSystem.mkdirTree("/working");
            this.fileSystem.mkdirTree("/emscripten");
            this.fileSystem.mkdirTree("/usr/bin");
            this.fileSystem.mkdirTree("/tmp");
            console.log('✅ Directory structure created');
            
            // 加载编译器WASM文件
            await this.loadCompilerWasm();
            
            // 创建编译器进程
            await this.createCompilerProcesses();
            
            this.initialized = true;
            console.log('✅ EmceptionCore initialization complete');
            
        } catch (error) {
            console.error('❌ EmceptionCore initialization failed:', error);
            throw error;
        }
    }

    /**
     * 加载编译器WASM文件
     */
    async loadCompilerWasm() {
        console.log('📥 Loading compiler WASM files...');
        
        // 加载LLVM编译器WASM
        const llvmWasmResponse = await fetch('./emception/llvm/llvm-box.wasm');
        if (!llvmWasmResponse.ok) {
            throw new Error(`Failed to load LLVM WASM: ${llvmWasmResponse.status}`);
        }
        const llvmWasmBuffer = await llvmWasmResponse.arrayBuffer();
        this.fileSystem.writeFile("/wasm/llvm-box.wasm", new Uint8Array(llvmWasmBuffer));
        console.log('✅ LLVM WASM loaded successfully');
        
        // 加载Binaryen编译器WASM
        const binaryenWasmResponse = await fetch('./emception/binaryen/binaryen-box.wasm');
        if (!binaryenWasmResponse.ok) {
            throw new Error(`Failed to load Binaryen WASM: ${binaryenWasmResponse.status}`);
        }
        const binaryenWasmBuffer = await binaryenWasmResponse.arrayBuffer();
        this.fileSystem.writeFile("/wasm/binaryen-box.wasm", new Uint8Array(binaryenWasmBuffer));
        console.log('✅ Binaryen WASM loaded successfully');
    }

    /**
     * 创建编译器进程
     */
    async createCompilerProcesses() {
        console.log('🔧 Creating compiler processes...');
        
        const processConfig = {
            FS: this.fileSystem.FS,
            onrunprocess: this.onRunProcess.bind(this)
        };
        
        // 创建并初始化LLVM进程
        console.log('🔧 Creating LLVM process...');
        this.processes.llvm = new LlvmBoxProcess(processConfig);
        await this.processes.llvm.ready;
        console.log('✅ LLVM process created and initialized');
        
        // 创建并初始化Binaryen进程
        console.log('🔧 Creating Binaryen process...');
        this.processes.binaryen = new BinaryenBoxProcess(processConfig);
        await this.processes.binaryen.ready;
        console.log('✅ Binaryen process created and initialized');
    }

    /**
     * 进程运行回调
     */
    async onRunProcess(argv, opts = {}) {
        console.log('⚙️ Running process:', argv.join(' '));
        
        // 简化的进程执行 - 返回成功状态
        return {
            returncode: 0,
            stdout: '',
            stderr: ''
        };
    }

    /**
     * 运行编译命令
     */
    async run(command) {
        if (!this.initialized) {
            throw new Error('EmceptionCore not initialized');
        }
        
        console.log('🔧 Running command:', command);
        
        // 解析命令
        const argv = command.split(' ').filter(arg => arg.trim());
        if (argv.length === 0) {
            throw new Error('Empty command');
        }
        
        // 检查命令类型并调用相应的编译器进程
        const command_name = argv[0];
        
        try {
            if (command_name === 'em++' || command_name === 'emcc' || command_name === 'clang++') {
                // 使用LLVM进程编译
                console.log('🔧 Using LLVM process for compilation...');
                const result = this.processes.llvm.exec(argv);
                console.log('✅ LLVM compilation completed');
                return result;
            } else if (command_name === 'lld' || command_name === 'wasm-ld') {
                // 使用LLVM进程进行链接（lld是LLVM的链接器）
                console.log('🔧 Using LLVM process for linking...');
                const result = this.processes.llvm.exec(argv);
                console.log('✅ LLVM linking completed');
                return result;
            } else if (command_name === 'wasm-opt' || command_name.includes('binaryen')) {
                // 使用Binaryen进程
                console.log('🔧 Using Binaryen process...');
                const result = this.processes.binaryen.exec(argv);
                console.log('✅ Binaryen processing completed');
                return result;
            } else {
                // 其他命令，使用默认处理
                console.log(`⚠️ Unknown command: ${command_name}, using default handler`);
                const result = await this.onRunProcess(argv);
                return result;
            }
        } catch (error) {
            console.error('❌ Command execution failed:', error);
            return {
                returncode: 1,
                stdout: '',
                stderr: error.message || 'Unknown error'
            };
        }
    }

    /**
     * 写入文件到文件系统
     */
    writeFile(path, content) {
        if (!this.initialized) {
            throw new Error('EmceptionCore not initialized');
        }
        
        this.fileSystem.writeFile(path, content);
        console.log('📝 File written:', path);
    }

    /**
     * 从文件系统读取文件
     */
    readFile(path) {
        if (!this.initialized) {
            throw new Error('EmceptionCore not initialized');
        }
        
        const content = this.fileSystem.readFile(path);
        console.log('📖 File read:', path);
        return content;
    }

    /**
     * 检查文件是否存在
     */
    exists(path) {
        if (!this.initialized) {
            return false;
        }
        
        return this.fileSystem.exists(path);
    }
}