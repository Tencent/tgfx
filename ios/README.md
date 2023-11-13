# Hello2D-iOS

A minimal project that demonstrates how to integrate the tgfx library into your iOS project.

## Prerequisites

Before you begin building the demo project, please make sure to carefully follow the instructions
provided in the [README.md](../README.md) file located in the root directory. That documentation
will guide
you through the necessary steps to configure your development environment.

## Build & Run

Run the following command in the ios/ directory:

```
./gen [-p ios|simulator] [-a arm64|x64] [-Dcmake_variable=value]...
```

The `-a` option is used to specify the project architecture that accepts either `arm64` or `x64`.
The `-a x64` option is valid only with `-p simulator`. If no platform is specified, the script
will default to `ios`. Additionally, you can pass cmake options using the `-D` option. For example,
if you want to generate a project with webp encoding support for the arm64 simulator, you can
execute the following command:

```
./gen -p simulator -a arm64 -DTGFX_USE_WEBP_ENCODE=ON
```

And then, launch XCode and open the ios/Hello2D.xcworkspace. Once you've done that, you'll be all
set and ready to go!

