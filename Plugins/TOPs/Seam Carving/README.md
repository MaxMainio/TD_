# Seam Carving

TouchDesigner TOP custom operator that resizes an image horizontally by removing or inserting low-cost vertical seams.

## TouchDesigner Metadata

- Label: `Seam Carving`
- Type: `Seamcarving`
- Inputs: 1-2 TOPs
- Execute mode: CPU memory
- Native output: `RGBA32Float`

The `Type` value is compatibility-sensitive. Do not rename it casually, because existing `.toe` files may reference it.

## Inputs

Input 0 is the source image. It is downloaded as `RGBA32Float`.

Input 1 is optional external seam energy. It is downloaded as `Mono32Float`. If its dimensions do not match the source image, it is resized to the source dimensions with nearest-neighbor sampling. When input 1 is not connected, the operator computes internal energy from source-image luminance gradients.

Lower energy values are preferred by the seam tracing path.

## Parameters

| Page | Parameter | Values | Notes |
| --- | --- | --- | --- |
| Carving | Carve Width | -256 to 256 | Positive values remove vertical seams and reduce output width. Negative values insert seams and increase output width. Zero passes the image through. |
| Carving | Cost Update | `Once`, `Targeted`, `Global` | Controls how often cumulative cost is recomputed while multiple seams are removed or inserted. |

## Cost Update Modes

- `Once`: compute the cumulative cost map once, then shrink it as seams are processed.
- `Targeted`: update a local band around each changed seam path.
- `Global`: recompute energy and cumulative cost for each seam.

`Targeted` is the default. It is usually a practical balance between speed and quality.

## Output

The native output is `RGBA32Float`. Height matches the source image. Width is adjusted by `Carve Width`, subject to safety clamps in the implementation.

Seam insertion creates new pixels by averaging neighboring source pixels around the selected seam path.

The Common page `Output Resolution`, `Output Aspect`, and `Pixel Format` settings are honored during final upload. When Common `Pixel Format` is `Use Input`, the output is repacked to the connected input TOP's pixel format.

## Dependencies

- TouchDesigner C++ TOP headers
- C++17 compiler
- OpenCV `core`, `imgproc` (used for Common output resizing)

OpenCV is an external dependency and is not embedded in the bundle. A locally
built plugin requires that compatible OpenCV installation at runtime. Compiled
binary distribution needs separate dependency packaging.

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

The finished bundle is `plugin/SeamCarving.plugin`; intermediates stay in `build/`.
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
