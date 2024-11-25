import Figma from '../wasm-mt/figma.js';
import * as types from "../../types/types"; // 导入 FigmaRenderer
import {TGFXBind} from '../../lib/tgfx.js';

if (typeof window !== 'undefined') {
    window.onload = async () => {
        try {
            const figma = await Figma({
                locateFile: (file) => './wasm-mt/' + file
            }).then((module: types.TGFX) => {
                TGFXBind(module);
                return module;
            }).catch((error: any) => {
                console.error(error);
                throw new Error("Hello2D init failed. Please check the .wasm file path!.");
            });


            // 发送自定义事件，传递 figma 模块
            const event = new CustomEvent('wasmLoaded', {detail: figma});
            document.dispatchEvent(event);
        } catch (error) {
            console.error(error);
            throw new Error("TGFX init failed. Please check the .wasm file path!.");
        }
    }
}