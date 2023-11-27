#!/usr/bin/env node
const childProcess = require('child_process');
const os = require('os');
const fs = require('fs');
const path = require('path');

let sourceDir = path.resolve(__dirname, "../demo");
let buildDir = path.resolve(sourceDir, "cmake-build-release");
let buildType = "Release";
let args = process.argv.slice(2);
if (args.indexOf("-d") !== -1 || args.indexOf("--debug") !== -1) {
    buildDir = path.resolve(sourceDir, "cmake-build-debug");
    buildType = "Debug";
}
let options = {
    shell: os.platform() === "win32" ? "cmd.exe" : true,
    maxBuffer: 50 * 1024 * 1024
}
let currentVersion = childProcess.execSync("emcc --version", options).toString();
if (fs.existsSync(buildDir)) {
    let version = "";
    let versionFile = path.resolve(buildDir, "emscripten_version.txt");
    try {
        version = fs.readFileSync(versionFile, "utf-8").toString();
    } catch (e) {
    }
    if (version !== currentVersion) {
        try {
            fs.rmSync(buildDir, {recursive: true, force: true});
        } catch (e) {
        }
    }
}
let wasmDir = path.join(sourceDir, "wasm");
try {
    fs.rmSync(wasmDir, {recursive: true, force: true});
    fs.mkdirSync(wasmDir);
} catch (e) {
}
let cmd = "emcmake cmake -S " + sourceDir + " -B " + buildDir + " -G Ninja -DCMAKE_BUILD_TYPE=" + buildType;
process.stdout.write(cmd);
options.stdio = "inherit";
childProcess.execSync(cmd, options);
try {
    fs.writeFileSync(buildDir + "/emscripten_version.txt", currentVersion);
} catch (err) {
    console.error(err);
}
cmd = "cmake --build " + buildDir + " --target hello2d";
process.stdout.write(cmd);
childProcess.execSync(cmd, options);
fs.copyFileSync(buildDir + "/hello2d.js", path.join(wasmDir, "hello2d.js"));
fs.copyFileSync(buildDir + "/hello2d.wasm", path.join(wasmDir, "hello2d.wasm"));

