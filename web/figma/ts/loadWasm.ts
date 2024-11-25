import Figma from '../wasm-mt/figma.js'; // 导入 FigmaRenderer
// import {TGFXBind} from '../../lib/tgfx.js';

(async () => {
    try {
        const figma = await Figma({
            locateFile: (file) => './wasm-mt/' + file
        });

        // 绑定 TGFX
        // TGFXBind(figma);

        // 发送自定义事件，传递 figma 模块
        const event = new CustomEvent('wasmLoaded', { detail: figma });
        document.dispatchEvent(event);
    } catch (error) {
        console.error(error);
        throw new Error("TGFX init failed. Please check the .wasm file path!.");
    }
})();