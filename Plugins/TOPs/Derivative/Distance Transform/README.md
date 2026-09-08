# Distance Transform TOP

Derivative-modified TOP sample that runs OpenCV distance transform on one selected channel from an input TOP. The TouchDesigner node label is `Distance Transform`, and the tested `opType` is `Distancetransform`.

## Input and Output

- Takes one TOP input.
- Downloads the input as `RGBA8Fixed`.
- Samples one channel from the input, selected by the `Channel` parameter.
- Native output data is `Mono32Float`.
- Honors the Common page `Output Resolution`, `Output Aspect`, and `Pixel Format` settings during final upload.
- When Common `Pixel Format` is `Use Input`, the distance result is repacked to the connected input TOP's pixel format.

## Parameters

- `Distancetype` / `Distance Type`: OpenCV distance metric: `L1`, `L2`, or `C`.
- `Masksize` / `Mask Size`: Transform mask: `Three` (`3x3`), `Five` (`5x5`), or `Precise`.
- `Normalize`: Normalizes the float output to 0 to 1 when enabled.
- `Channel`: Input channel to sample: `R`, `G`, `B`, or `A`.

## Dependencies

Requires OpenCV `core` and `imgproc`.

## Notes

- The input channel is treated as an 8-bit single-channel image before OpenCV distance transform.
- Common page `Fill Viewer` and `Viewer Smoothness` are handled by TouchDesigner at the viewer level.
- Derivative license headers must be preserved when editing or sharing this code.

## Build and install

Requirements: macOS, Apple's command-line developer tools with a C++17 compiler
and macOS SDK, CMake 3.20 or newer, and TouchDesigner for loading the plugin.
The SDK headers are included. The maintained TouchDesigner test target is
2025.33230; other host/OS versions require runtime verification. The default
architecture is the current Mac. Apple Silicon is the current validation platform;
physical Intel TouchDesigner loading and Windows builds remain unverified.

Install OpenCV `core` and `imgproc` for the same architecture. With Homebrew
installed, run `brew install cmake opencv`. If discovery fails, set `OpenCV_DIR`
to the folder containing `OpenCVConfig.cmake`, or set `CMAKE_PREFIX_PATH` to the
installation prefix. Runtime macOS requirements also depend on that OpenCV and
TouchDesigner build; the current Homebrew OpenCV validation build requires macOS 15.

From this downloaded or cloned plugin folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

The finished bundle is `plugin/DistanceTransform.plugin`; intermediates stay in `build/`.
`OUTPUT_DIRECTORY` can redirect bundle output for validation. To select an
architecture explicitly, configure a fresh build with
`-DCMAKE_OSX_ARCHITECTURES=arm64` or `-DCMAKE_OSX_ARCHITECTURES=x86_64`.
OpenCV must match the selected architecture; an arm64 library cannot link into an Intel bundle.

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
