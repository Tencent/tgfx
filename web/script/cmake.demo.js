#!/usr/bin/env node
process.chdir(__dirname);

const useWebGPU = process.argv.includes("--webgpu");
if (useWebGPU) {
    process.argv = process.argv.filter(arg => arg !== "--webgpu");
}

process.argv.push("-s");
process.argv.push("../demo");
process.argv.push("-o");
process.argv.push("../demo");
process.argv.push("-p");
process.argv.push("web");
if (useWebGPU) {
    process.argv.push("-DTGFX_USE_WEBGPU=ON");
}
process.argv.push("hello2d");
require("../../build_tgfx");

