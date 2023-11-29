#!/usr/bin/env node
process.chdir(__dirname);

process.argv.push("-s");
process.argv.push("../demo");
process.argv.push("-o");
process.argv.push("../demo");
process.argv.push("-p");
process.argv.push("web");
process.argv.push("hello2d");
require("../../build_tgfx");

