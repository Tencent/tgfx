# Hello2D-iOS

A minimal project that demonstrates how to integrate the tgfx library into your iOS project.

## Prerequisites

Before you begin building the demo project, please make sure to carefully follow the instructions
provided in the [README.md](../README.md) file located in the root directory. That documentation will guide 
you through the necessary steps to configure your development environment.

## Build & Run

Run the following command in the ios/ directory:

```
./gen_project -a [arch]
```

The [arch] option can take one of the following values: arm64, x64, arm64-simulator. The 'x64' and 
'arm64-simulator' arches are intended for building simulators. If no architecture is specified, the 
script will default to 'arm64'.

Next, open the ios/Hello2D.xcworkspace using XCode, and you'll be good to go!

