import hello2d from 'libhello2d.so';
import display from '@ohos.display';
import { image } from '@kit.ImageKit';

@Entry
@Component
struct Index {
  @State message: string = 'Hello TGFX';
  private drawIndex: number = 1;

  aboutToAppear(): void {
    let displayInfo = display.getDefaultDisplaySync();
    hello2d.updateDensity(displayInfo.densityPixels);
    getContext(this).resourceManager.getRawFileContent("bridge.jpg", async (error, value) => {
      if (error) {
        return;
      }
      let name = "bridge";
      //from pixelmap
      const buffer = value.buffer.slice(0);
      let img = image.createImageSource(buffer);
      let pixelmap = img.createPixelMapSync();
      hello2d.addImageFromPixelMap(name, pixelmap);
      pixelmap.release();

      // from data
      // const buffer = value.buffer.slice(0);
      // hello2d.addImageFromEncoded(name, buffer);
    })
  }

  build() {
    Row() {
      Column() {
        XComponent({
          id: "xcomponent",
          type: XComponentType.SURFACE,
          libraryname: "hello2d"
        })
      }
      .width('100%')
      .onClick(() => {
        hello2d.draw(this.drawIndex++);
      })
    }
    .height('100%')
  }
}
