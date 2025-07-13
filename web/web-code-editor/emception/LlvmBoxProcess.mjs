import BoxProcess from "./BoxProcess.mjs";
import LlvmBoxModule from "./llvm/llvm-box.mjs";

const tool_mapping = {
    "clang++": "clang",
    "wasm-ld": "lld",
};

export default class LlvmBoxProcess extends BoxProcess {
    constructor(opts) {
        const wasmBinary = opts.FS.readFile("/wasm/llvm-box.wasm");
        super(LlvmBoxModule, { ...opts, wasmBinary, tool_mapping });
    }
};
