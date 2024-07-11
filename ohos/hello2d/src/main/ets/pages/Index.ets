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
    getContext(this).resourceManager.getRawFileContent("rotation.jpg", async (error, value) => {
      let name = "bridge";
      //from pixelmap
      // const buffer = value.buffer.slice(0);
      // let img = image.createImageSource(buffer);
      // let pixelmap = img.createPixelMapSync();
      // let imgInfo = await img.getImageInfo(0);
      // let orientation = await img.getImageProperty(image.PropertyKey.ORIENTATION);
      // if (orientation) {
      //   hello2d.addImageFromPixelMap(name, pixelmap, orientation, imgInfo.alphaType);
      // } else {
      //   hello2d.addImageFromPixelMap(name, pixelmap, "Top-left", imgInfo.alphaType);
      // }
      // pixelmap.release();

      // from data
      // const buffer = value.buffer.slice(0);
      // hello2d.addImageFromEncoded(name, buffer);

      // from source
      const buffer = value.buffer.slice(0);
      let img = image.createImageSource(buffer);
      hello2d.addImageFromSource(name, img);
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
