import hello2d from 'libhello2d.so';
import display from '@ohos.display';

@Entry
@Component
struct Index {
  @State message: string = 'Hello TGFX';
  private drawIndex: number = 0;
  private offsetX: number = 0; // px
  private offsetY: number = 0; // px
  private zoom: number = 1;
  private lastEventScale: number = 1;
  private scaleChangeThreshold: number = 0.8;
  private pinchStartZoom: number = 1;
  private lastPinchCenterX: number = 0;
  private lastPinchCenterY: number = 0;
  private lastPanX: number = 0;
  private lastPanY: number = 0;

  aboutToAppear(): void {
    let displayInfo = display.getDefaultDisplaySync();
    hello2d.updateDensity(displayInfo.densityPixels);
    // Load all image resources before starting the draw loop. The native LayerBuilders require
    // both "bridge" and "TGFX" images to be registered; starting the loop before they are ready
    // causes null-pointer crashes in the layer tree construction.
    let loadedCount = 0;
    const totalImages = 2;
    const onImageLoaded = () => {
      loadedCount++;
      if (loadedCount >= totalImages) {
        hello2d.startDrawLoop();
      }
    };
    getContext(this).resourceManager.getRawFileContent("bridge.jpg", async (error, value) => {
      if (!error) {
        const buffer = value.buffer.slice(0);
        hello2d.addImageFromEncoded("bridge", buffer);
      }
      onImageLoaded();
    });
    getContext(this).resourceManager.getRawFileContent("tgfx.png", async (error, value) => {
      if (!error) {
        const buffer = value.buffer.slice(0);
        hello2d.addImageFromEncoded("TGFX", buffer);
      }
      onImageLoaded();
    });
  }

  onPageHide(): void {
    hello2d.stopDrawLoop();
  }

  onPageShow(): void {
    hello2d.startDrawLoop();
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
        this.offsetX = 0;
        this.offsetY = 0;
        this.zoom = 1;
        this.drawIndex += 1;
        hello2d.updateDrawParams(this.drawIndex, this.zoom, this.offsetX, this.offsetY);
      }, 5)
      .gesture(
        GestureGroup(GestureMode.Parallel,
          PinchGesture({ fingers: 2, distance: 1 })
            .onActionStart((event: GestureEvent | undefined) => {
              if (event) {
                this.pinchStartZoom = this.zoom;
                this.lastEventScale = 1;
                this.lastPinchCenterX = (event.fingerList[0].displayX + event.fingerList[1].displayX) / 2.0;
                this.lastPinchCenterY = (event.fingerList[0].displayY + event.fingerList[1].displayY) / 2.0;
              }
            })
            .onActionUpdate((event: GestureEvent | undefined) => {
              if (event) {
                // Limit scale changes to prevent image jitter caused by simultaneous scaling and panning
                // which can cause sudden jumps in event.scale values
                let eventScale = event.scale;
                if (event.scale - this.lastEventScale >= this.scaleChangeThreshold) {
                  eventScale = this.lastEventScale + this.scaleChangeThreshold;
                } else if (event.scale - this.lastEventScale <= -this.scaleChangeThreshold) {
                  eventScale = this.lastEventScale - this.scaleChangeThreshold;
                }
                this.lastEventScale = eventScale;
                const zoomNew = Math.max(0.001, Math.min(1000, this.pinchStartZoom * eventScale));
                this.offsetX = (this.offsetX - vp2px(this.lastPinchCenterX)) * (zoomNew / this.zoom) + vp2px(this.lastPinchCenterX);
                this.offsetY = (this.offsetY - vp2px(this.lastPinchCenterY)) * (zoomNew / this.zoom) + vp2px(this.lastPinchCenterY);
                this.zoom = zoomNew;
                this.lastPinchCenterX = (event.fingerList[0].displayX + event.fingerList[1].displayX) / 2.0;
                this.lastPinchCenterY = (event.fingerList[0].displayY + event.fingerList[1].displayY) / 2.0;
                hello2d.updateDrawParams(this.drawIndex, this.zoom, this.offsetX, this.offsetY);
              }
            })
            .onActionEnd((event: GestureEvent | undefined) => {
              this.lastEventScale = 1;
            }),

          PanGesture({ fingers: 1, distance: 1 })
            .onActionStart((event: GestureEvent | undefined) => {
              if (event) {
                this.lastPanX = event.offsetX;
                this.lastPanY = event.offsetY;
              }
            })
            .onActionUpdate((event: GestureEvent | undefined) => {
              if (event) {
                this.offsetX += vp2px(event.offsetX - this.lastPanX);
                this.offsetY += vp2px(event.offsetY - this.lastPanY);
                this.lastPanX = event.offsetX;
                this.lastPanY = event.offsetY;
                hello2d.updateDrawParams(this.drawIndex, this.zoom, this.offsetX, this.offsetY);
              }
            })
            .onActionEnd((event: GestureEvent | undefined) => {
            })
        )
      )
    }
    .height('100%')
  }
}
