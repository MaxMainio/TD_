# Error Diffusion Dither

A C++ TOP port of Max Mainio Beidler's Error Diffusion Dither COMP. Processes
the current frame synchronously with eight original diffusion kernels, corrected
boundaries, and explicit color/alpha controls. No Python, NumPy, OpenCV, Metal,
or third-party dynamic libraries are needed by the plugin.

## Operator

- Label: **Error Diffusion Dither**
- Type: `Errordiffusiondither` (compatibility-sensitive)
- Inputs: one non-empty 2D TOP; one image output
- Execute mode: `CPUMem`; one RGBA32Float download and one packed upload
- C++17; native macOS architecture by default, macOS 12.0 deployment target
- Initial host-validation target: TouchDesigner 2025.33230

## Parameters

| Page | Parameter (identifier) | Values | Default |
| --- | --- | --- | --- |
| Dither | Algorithm (`Algorithm`) | Floyd-Steinberg, Jarvis-Judice-Ninke, Stucki, Atkinson, Burkes, Sierra | Floyd-Steinberg |
| Dither | Sierra Variant (`Alternate`) | Full, Two-Row, Lite; enabled for Sierra | Full |
| Dither | Bit Depth (`Bitdepth`) | Integer 1-8 per processed channel | 1 |
| Dither | Scan Pattern (`Scanpattern`) | Raster, Serpentine | Raster |
| Dither | Diffusion Strength (`Diffusion`) | 0-1; 0 is plain quantization | 1 |
| Color | Color Mode (`Colormode`) | RGB, Monochrome | RGB |
| Color | Monochrome Source (`Monosource`) | Luminance, Red, Green, Blue, Alpha; enabled for Monochrome | Luminance |
| Color | Alpha (`Alpha`) | Preserve, Dither, Opaque | Preserve |
| Common | Pixel Format (`format`) | Use Input or supported storage format | Use Input |

Bit Depth is quantization depth, not texture storage depth: 1 bit gives two
levels per color channel (up to eight RGB colors); 2 bits gives four levels per
channel. Pixel Format controls how those results are stored. A low-precision
storage format can approximate the selected levels, particularly for alpha in
RGB10A2. Use RGBA32Float when inspecting exact numerical results.

Monochrome computes `0.2126 R + 0.7152 G + 0.0722 B`, or the selected source
channel, before quantization and replicates that result into RGB. Mono and
Mono+Alpha storage use processed R, without mixing already-dithered RGB.
RGB mode with Mono storage therefore dithers the source red channel. RG stores
processed red/green; A stores the alpha selected by the Alpha parameter.
Channels absent from the output are not needlessly dithered.

Common Output Resolution and Output Aspect are honored. Resizing uses pixel-center
nearest-neighbor sampling **before** dithering. Use an upstream Resolution TOP
for filtered resizing; this plugin does not use Common Input Smoothness to
change its sampling method. Viewer Smoothness affects viewing, not the pixels.

Supported storage: RGBA/BGRA 8-bit, RGBA 16-bit fixed/float and 32-bit float;
Mono, RG, A, and Mono+Alpha at 8/16-bit fixed and 16/32-bit float;
RGB10A2 and RGB11Float. Unknown formats fall back to BGRA8 with a warning.
The SDK describes 23 concrete formats (BGRA and RGBA count separately).

## Algorithm and compatibility

Source values are scaled by `2^Bitdepth - 1`. Each accumulated pixel is rounded
to the nearest even integer at halfway values, then its error is multiplied by
Diffusion Strength and distributed using the selected kernel. Intermediate
values remain unclipped; normalized dithered output is clamped to 0-1.
Atkinson distributes six eighths of the error, intentionally discarding a quarter.
Float32 accumulation order is preserved; fast-math and fused multiply/add are
disabled for the core.

Raster scans left-to-right from the bottom image row upward. Serpentine scans
right-to-left on alternate rows and mirrors all horizontal kernel offsets.
Every image pixel is quantized. Out-of-image contributions are discarded without
renormalization. There is no COMP padding/cropping pre-roll, and Sierra Lite's
unprocessed outer columns are corrected. These changes can propagate pattern
differences into the interior; this is not a pixel-identical legacy-render mode.

RGB channels are independent. Alpha defaults to Preserve; choose Dither to
reproduce the COMP's RGBA alpha treatment. Preserve retains finite input alpha
subject to storage precision. Opaque writes 1. There is no gamma transform or
unpremultiplication; downloads and uploads request color-space passthrough.
NaN input becomes 0; positive/negative infinity become 1/0. Extremely large
finite color values are bounded before scaling to avoid float overflow while
retaining their saturated output. Missing/unsupported input reports an error;
the last successful texture can remain visible until a valid input cooks.

The original `Algorithm`, `Alternate`, and `Bitdepth` identifiers and menu order
are retained. This standalone TOP is not an automatic replacement for COMP
paths or parameter expressions. Sierra's displayed sub-menu label is clearer,
and Pixel Format lives on Common instead of a duplicated custom parameter.

## Performance and ownership

Only requested cooks run. A bounded pool of up to three persistent workers
processes independent channels; the cook thread also participates. Small images
run inline. All jobs complete before returning, and workers never call host APIs.
Destructor shutdown wakes and joins every worker. There is no frame queue or
background output latency.

Each processed channel reuses at most three scratch rows, plus its full output
plane. Buffers retain capacity for reuse after dimension changes; they do not
grow on repeated cooks at the same dimensions. Deleting the node releases them.

Info CHOP channels:

- `download_ms`: download submission and waiting for input pixels
- `process_ms`: source preparation, diffusion, alpha, and worker synchronization
- `pack_ms`: output-buffer allocation and pixel packing
- `execute_ms`: total CPU execute duration, including upload submission
- `workers`: allocated background worker threads (not total CPU cores in use)
- `scratch_bytes`, `retained_bytes`: engine scratch size and retained buffer capacity

Upload GPU completion is not included in CPU timings. See the
[validation and benchmark record](../../../docs/error-diffusion-dither-validation.md)
for measured results and remaining in-host checks. 1080p/60 is a target, not a
guarantee; strict diffusion retains sequential dependencies within each channel.

## Build and install

Requirements: CMake 3.20 or newer, Apple's command-line developer tools with a
macOS SDK, and a compatible TouchDesigner installation for loading the plugin.
The build uses C++17 and defaults to a macOS 12.0 deployment target. No OpenCV
installation or downloaded build dependencies are needed.

From this plugin's source folder (also works after extracting a standalone
source ZIP):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Intermediate build files stay in this source folder's ignored `build/` directory.
The finished bundle is written to `plugin/ErrorDiffusionDither.plugin` beside it.
An optional `OUTPUT_DIRECTORY` override is available for isolated validation;
copy bundles to `dist/Plugins/custom/` only when preparing an install or release.

This folder includes a local copy of `TOPOutputHelper.h` so the build does not
depend on files elsewhere in the workspace. The copy matches
`Plugins/shared/TOPOutputHelper.h` as of 2026-09-07. Future fixes to the shared
helper should be reviewed and copied here deliberately, preserving the local
single-channel packing behavior in `OutputPacking.h`.

For a source release, include this folder's source/header files, `CMakeLists.txt`,
`Info.plist`, README, and `tests/`. Omit generated `build/`, `plugin/`, and Python
cache folders. Keep the SDK license headers intact. The linked benchmark record
is part of the full workspace documentation and is not required to build.

The SDK headers were copied unchanged from the maintained
Basic Filter plugin, with Derivative license headers preserved. No source or
SDK files in the imported reference snapshot were changed.

For a quick test, select the built `ErrorDiffusionDither.plugin` in a CPlusPlus
TOP's **Plugin Path** parameter. For the named operator in the Create menu,
copy the entire bundle into a `Plugins` folder beside your `.toe`, or into
`~/Library/Application Support/Derivative/TouchDesigner099/Plugins`, then restart
TouchDesigner. Complete TouchDesigner's normal plugin trust prompt if shown.
The source does not require Apple Silicon: the default build uses the native
compiler architecture, so an Intel Mac normally produces an `x86_64` bundle and
an Apple Silicon Mac produces an `arm64` bundle. A prebuilt `arm64` bundle cannot
load in Intel TouchDesigner. Match the architecture of the TouchDesigner process.
To explicitly target Intel, configure a fresh build directory with
`-DCMAKE_OSX_ARCHITECTURES=x86_64`; a Universal build uses
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and requires SDK support for both.
The standalone source package builds for both `arm64` and `x86_64`. Native ARM
tests and the Intel test executable under Rosetta passed on 2026-09-07. Actual
Intel Mac TouchDesigner loading and performance remain unvalidated.

No custom entitlements or rpaths are needed for the system-only dependencies.
The build ad-hoc signs the complete local bundle (`TD_ADHOC_SIGN=ON`); this is not a notarized public
release. Check `otool -L` and validate loading before sharing compiled bundles.

## Tests and host validation

CTest covers 6,912 settings combinations against a separate full-image reference,
edges/narrow images, ties, resampling, channel masks, worker parity and shutdown,
non-finite input, buffer guards, and pixel packing. It is included in the workspace
build-check script. `DitherBenchmark [samples=9] [workers=3]` emits CSV for every
kernel in RGB/Monochrome at 512x512 and 1920x1080 (RGBA8 output, preserved alpha).

Optional NumPy reference checks, using Python with NumPy installed:

```sh
python3 tests/python_reference.py --driver build/DitherOracleDriver
```

Add `--archive /path/to/Error_Diffusion_Dither.tox.zip` to compare with the eight
original callbacks instead of the independent matrix reference. The reviewed
callbacks execute locally; other DATs and archive contents are not run. Optional
`--image /path/to/photo.tif` adds a resized photograph fixture (requires Pillow)
alongside gradients and transparent imagery. Optional
`--benchmark /tmp/original-dither.csv` times those original callbacks; the 1080p
Python cases can take several minutes. No input images or models are downloaded.

For host checks, execute `tests/touchdesigner_validate.py` in TouchDesigner's
Python Textport, then call `run_validation('/absolute/path/ErrorDiffusionDither.plugin')`.
It creates a uniquely named Base COMP under `/project1`, checks pixel comparisons,
parameter enabling, formats, resolution/aspect, reconnects, and creation/deletion,
and writes a temporary JSON report. It does not save or replace your project.
The script is syntax-checked but requires a real host run before results can be
claimed. Also compare gradients, photographs and transparent imagery visually
against the original COMP, and profile live video with Performance Monitor.

Original documentation: https://maxmain.io/touchdesigner-components/tops/dithering/#error-diffusion-dither
