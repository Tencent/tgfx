import { hapTasks } from '@ohos/hvigor-ohos-plugin';
import * as fs from 'fs';
import * as path from 'path';

const rawfileDir = path.resolve(__dirname, 'src/main/resources/rawfile');
const assetsDir = path.resolve(__dirname, '../../resources/assets');
const files = ['tgfx.png', 'bridge.jpg'];

for (const file of files) {
  const src = path.join(assetsDir, file);
  const dst = path.join(rawfileDir, file);
  if (fs.existsSync(src)) {
    fs.mkdirSync(rawfileDir, { recursive: true });
    fs.copyFileSync(src, dst);
  }
}

export default {
    system: hapTasks,
    plugins: []
}
