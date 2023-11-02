import { TGFXInit, types } from '../src/main';

let TGFXModule: types.TGFX;

window.onload = async () => {
  TGFXModule = await TGFXInit({ locateFile: (file: string) => '../lib/' + file });
  console.log(`wasm loaded!`, TGFXModule);
  document.getElementById('waiting')!.style.display = 'none';
  document.getElementById('container')!.style.display = 'block';
  TGFXModule.ScalerContext.isEmoji('测试');
  TGFXModule.ScalerContext.isEmoji('👍');
  if (TGFXModule.TGFXView) {
    const view = TGFXModule.TGFXView._Make('#tgfx');
    document.getElementById('tgfx')!.onclick = () => {
      view.draw();
    };
    view.draw();
  }
};
