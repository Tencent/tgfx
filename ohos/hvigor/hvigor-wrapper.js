const fs = require('fs');
const path = require('path');
const os = require('os');

// 官方标准配置
const hvigorHome = path.join(os.homedir(), '.hvigor');
const cacheDir = path.join(hvigorHome, 'cache');

// 确保目录存在
if (!fs.existsSync(hvigorHome)) {
  fs.mkdirSync(hvigorHome, { recursive: true });
}
if (!fs.existsSync(cacheDir)) {
  fs.mkdirSync(cacheDir, { recursive: true });
}

// 设置环境变量
process.env.HVIGOR_HOME = hvigorHome;
process.env.HVIGOR_CACHE_DIR = cacheDir;

// 查找并运行 hvigor
const localHvigorScript = path.join(__dirname, '..', 'node_modules', '@ohos', 'hvigor', 'bin', 'hvigor.js');
const hvigorScript = path.join(hvigorHome, 'bin', 'hvigor.js');
const hvigorBin = path.join(hvigorHome, 'hvigor');

if (fs.existsSync(localHvigorScript)) {
  require(localHvigorScript);
} else if (fs.existsSync(hvigorScript)) {
  require(hvigorScript);
} else if (fs.existsSync(hvigorBin)) {
  const { spawn } = require('child_process');
  const child = spawn(hvigorBin, process.argv.slice(2), { stdio: 'inherit' });
  child.on('exit', (code) => process.exit(code || 0));
} else {
  console.error('Hvigor not found. Please install hvigor first.');
  console.error('Run: npm install @ohos/hvigor');
  process.exit(1);
}