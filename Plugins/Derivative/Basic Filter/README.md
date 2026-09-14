# Basic Filter TOP

Derivative-modified TOP sample that limits the number of color levels in an input image. The TouchDesigner node label is `Filter`, and the tested `opType` is `Basicfilter`.

## Input and Output

- Takes one TOP input.
- Cooks in CPU memory.
- Processes internally as BGRA8 CPU pixels.
- Honors the Common page `Output Resolution`, `Output Aspect`, and `Pixel Format` settings during final upload.
- When Common `Pixel Format` is `Use Input`, the output is repacked to the connected input TOP's pixel format.

## Parameters

- `Bitspercolor` / `Bits per Color`: Number of retained bits per RGB channel, clamped from 1 to 8.
- `Dither`: Enables Floyd-Steinberg-style error diffusion.
- `Multithreaded`: Processes frames on worker threads. This mode intentionally returns completed work from earlier queued frames, so the output can lag the input by several frames.

## Dependencies

This plugin uses the TouchDesigner C++ headers and standard C++ only. It does not require OpenCV.

## Notes

- The visible label `Filter` is generic and may be collision-prone for broader distribution. Do not rename it without accepting saved-project compatibility impact.
- Common page `Fill Viewer` and `Viewer Smoothness` are handled by TouchDesigner at the viewer level.
- Derivative license headers must be preserved when editing or sharing this code.

## Build and install

Requirements: macOS, Apple's command-line developer tools with a C++17 compiler
and macOS SDK, CMake 3.20 or newer, and TouchDesigner for loading the plugin.
The SDK headers are included. The maintained TouchDesigner test target is
2025.33230; other host/OS versions require runtime verification. The default
architecture is the current Mac. Apple Silicon is the current validation platform;
physical Intel TouchDesigner loading and Windows builds remain unverified.

No external library download is required.

From this downloaded or cloned plugin folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The finished bundle is `plugin/BasicFilter.plugin`; intermediates stay in `build/`.
`OUTPUT_DIRECTORY` can redirect bundle output for validation. To select an
architecture explicitly, configure a fresh build with
`-DCMAKE_OSX_ARCHITECTURES=arm64` or `-DCMAKE_OSX_ARCHITECTURES=x86_64`.

Copy the complete bundle into a `Plugins` folder beside your `.toe`, or into
`~/Library/Application Support/Derivative/TouchDesigner099/Plugins`, then restart
TouchDesigner. Local builds do not install the plugin automatically. Validate
loading and linked dependencies before sharing compiled bundles.

## Source and attribution

All project sources and headers are included locally. `TOPOutputHelper.h` is an
unchanged copy of the maintained Common output helper dated 2026-09-07 (SHA-256
`fb110bc00dd67e8aefaa205508c9e7fbb2fbf2f79a5fd6f95e93cdff665b47b1`).
The SDK headers and adapted sample files retain their original Derivative
copyright and license notices. Preserve those notices when sharing source.
