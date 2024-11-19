#!/usr/bin/env node
process.chdir(__dirname);

process.argv.push("-s");
process.argv.push("../figma");
process.argv.push("-o");
process.argv.push("../figma");
process.argv.push("-p");
process.argv.push("web");
process.argv.push("figma");
require("../../build_tgfx");

