# Segment UV

TouchDesigner CPU TOP that finds connected alpha-mask segments and fills each
segment with a representative coordinate or repeatable random color. Version
**0.2.0** adds an automatically docked Info DAT describing each segment.

- Label: **Segment UV**. Type: `Segmentuv`. Bundle: `SegmentUV.plugin`.
- One TOP input. OpenCV `core` and `imgproc`; C++17.
- Native coordinate format: `RG32Float`, with RGBA32 internal processing.
- macOS source for arm64 and x86_64, with matching-architecture OpenCV and
  TouchDesigner. Current validation is arm64, TouchDesigner 2025.33230/Python
  3.11.15. Intel builds and live host acceptance remain unverified.

The original operator identity, input/image parameters, representative-pixel
selection, random draw order, output colors, and Common output behavior are
preserved. Existing nodes gain DAT Detail and the repair pulse.

## Parameters

| Page | Parameter (identifier) | Values / meaning | Default |
| --- | --- | --- | --- |
| Segmentation | Method (`Method`) | Centroid, Bounding-Box Center, Median Pixel Coordinate, Closest Segment Pixel to Centroid, Random | Closest Segment Pixel to Centroid |
| Segmentation | Seed (`Seed`) | Integer 1-9999; repeatable random colors | 1 |
| Segmentation | Alpha Threshold (`Alphathreshold`) | Include alpha strictly greater than this value, 0-1 | 0.5 |
| Output | DAT Detail (`Datdetail`) | Basic, Medium, Maximum; cumulative columns | Basic |
| Output | Create/Repair Segment DAT (`Repairsegmentdat`) | Create or repair the docked measurement table | Pulse |

The mask uses eight-neighbor connectivity: pixels touching at edges or corners
belong to one segment. Thresholding retains the original floating-point comparison
behavior, including positive infinity passing a finite threshold and NaN failing.
All image channels are downloaded as RGBA32Float, using the existing host color
space defaults; only alpha determines segmentation.

## Image output and coordinate methods

All pixels in a segment receive the same value. Coordinate methods write a
representative coordinate into red/green, blue 0, and alpha 1. Random writes
repeatable RGB colors with alpha 1. Background pixels are transparent black.

- Centroid rounds the arithmetic mean pixel index to the nearest integer, with
  halfway values rounded upward, then clamps it to the image.
- Bounding-Box Center uses `left + width / 2`, `top + height / 2` with integer
  division. Even-sized boxes choose the higher-index center pixel.
- Median Pixel Coordinate independently chooses the upper median of X and Y.
- Closest Segment Pixel to Centroid chooses a segment pixel nearest the unrounded
  centroid. Ties use the first encountered pixel in top-to-bottom, left-to-right
  scanning order. This is the only coordinate method guaranteed to select a
  foreground pixel for arbitrary nonconvex segments.
- Random resets the generator from Seed each cook. Colors are repeatable for an
  unchanged mask/seed within the same build; IDs and colors are not tracking data.
  OpenCV labeling and standard-library random implementations can differ across
  dependency/compiler versions.

The preserved image coordinate encoding is **top-down pixel-index based**:
`R = x / (input_width - 1)`, `G = y / (input_height - 1)`. A one-pixel dimension
produces 0 on that axis. The first and last pixel centers map to 0 and 1.
The upload restores the original image orientation. These encoded values differ
from the geometric coordinates in the DAT below.

Common Output Resolution uses the original OpenCV default linear resize before
final upload; boundary pixels can therefore become interpolated. Output Aspect
is honored. Common Pixel Format `Use Input` explicitly resolves the input format.
Storage formats can discard channels or quantize values, so select an RGBA format
to see all Random color channels. DAT output values precede resizing and packing.

## Docked segment table

Instance initialization schedules an Info DAT named `<node_name>_segments` at
frame end. It starts collapsed with its viewer enabled and opens directly beneath
the TOP, left-aligned with a 30-network-unit gap. No callback DAT is needed.
The table references `me.dock` and has Passive disabled for dependency updates.

Each larger detail includes all preceding columns in the same order:

| Detail | Total columns | Added columns |
| --- | --- | --- |
| Basic | 5 | `id`, `u`, `v`, `width`, `height` |
| Medium | 9 | `left`, `top`, `right`, `bottom` |
| Maximum | 15 | `selected_pixels`, `centroid_u`, `centroid_v`, `output_r`, `output_g`, `output_b` |

- `id` is a zero-based, frame-local row index in OpenCV foreground-label order.
  It is not a persistent identity. Only alpha is analyzed, so there is no source
  column. Background is excluded.
- `u/v` describe the bounding-box center. Width, height, and edges describe the
  segment's pixel footprint, including the full extent of its boundary pixels.
- Geometry uses a **bottom-left image-edge origin**, normalized by input width
  and height, matching Convex Hull. The first pixel center is
  `(0.5 / input_width, 0.5 / input_height)`. Right/top are exclusive pixel edges.
  `width = right - left`, `height = top - bottom`,
  `u = (left + right) / 2`, `v = (bottom + top) / 2`.
- `selected_pixels` is the exact integer foreground count, including neither
  holes nor pixels belonging to other segments inside its box.
- `centroid_u/v` are the unrounded arithmetic mean of selected pixel centers,
  normalized using the same bottom-left geometric convention.
- `output_r/g/b` are the segment's exact assigned float values before Common
  resizing/storage conversion. Coordinate methods expose their existing encoded
  coordinate and blue 0; Random exposes its three color values. These columns
  intentionally retain the image's original encoding described above.

Geometry is calculated at input resolution and remains available in every Method,
including Random. Common settings do not alter the table. Empty masks, missing
input, and failed processing produce the selected headers with no stale rows.
A complete snapshot is published only after output upload returns successfully.
On failure TouchDesigner may retain its previous image texture until recovery.

Matching tables are reused after initialization, copying, and project reload.
The reference follows the dock owner through rename/copy; the table's name may
remain unchanged after a TOP rename. Existing manual layout/viewer settings are
preserved. Repair restores the reference, placement, and viewer while preserving
whether an existing DAT is expanded or collapsed. Unrelated name collisions,
including errored Info DATs, are preserved. Deletion is respected until another
instance initialization or repair pulse. There is no recurring Python polling.

Docking errors appear as TOP warnings; processing and manually attached Info DATs
remain usable. Automatic setup requires an installed Custom OP; a generic
CPlusPlus TOP does not supply the Custom OP Python integration.

## Performance

The table reuses the existing connected-component bounds, areas, and centroids
and captures the already-assigned colors. It adds no second segmentation pass or
pixel scan. Only the selected columns are stored/formatted. DAT callbacks read
cached strings. Large segment counts add formatting, memory, and host table/viewer
cost; Basic is the default. Median and Closest methods retain their existing
per-segment pixel scans and can already be expensive on large masks.

The plugin now requests cooks on input/parameter changes, rather than on every
new frame it is asked for. All methods are deterministic for their inputs and
parameters. Animated inputs, expressions, and Seed changes still invalidate the
result through normal dependencies. The C++ tests verify scheduling flags and
cached reads; actual Info DAT scheduling/viewer cost requires host validation.

Native arm64 automated image, measurement, lifecycle/failure and simulated-host
docking tests passed. Live table scheduling/viewer performance, Intel builds and
older macOS compatibility remain unverified. For 10,000 separated 4x4 segments at
1080p with the default Method, measured median CPU time was 4.000 ms for the
previous algorithm and 5.843/7.513/9.411 ms for Basic/Medium/Maximum. These figures
exclude host transfers and table rendering and do not establish end-to-end FPS.
Reproduce the comparison against the included frozen previous algorithm with:

```sh
./build/SegmentBenchmark 41
```

The benchmark interleaves the previous algorithm and all three detail levels,
with three warmups and 41 measured samples for each 1080p case. It includes image
analysis, formatting, allocation, and cleanup. It excludes download, input clone,
output resizing/packing/upload, TouchDesigner scheduling, and table rendering.
The p95 uses the nearest-rank sample. Memory figures are conservative table-only
estimates, including inline string capacity twice; they are not process RSS.

## Build and install

Requirements:

- macOS and Apple's command-line developer tools with a C++17 compiler/macOS SDK.
  The default deployment target is macOS **15.0**, matching the OpenCV installation
  used for validation. Runtime requirements also depend on your TouchDesigner
  and OpenCV builds; changing the deployment target cannot lower their requirements.
- CMake 3.20 or newer.
- OpenCV with `core` and `imgproc`, matching the requested architecture.
- A TouchDesigner installation containing its Python framework/development headers.
  Rebuild against the intended host when its bundled Python version changes.
- A Python 3 interpreter when building tests.

Install the compiler tools using `xcode-select --install` if needed. With
[Homebrew](https://brew.sh/) installed, install the external build dependencies:

```sh
brew install cmake opencv
```

From this source folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The output is **`plugin/SegmentUV.plugin`**; intermediates stay in `build/`.
`TD_BUILD_TESTS` defaults to OFF. These generated directories are ignored and
must be excluded when sharing this ordinary, browsable source folder.

Configuration options:

- `TD_APPLICATION` defaults to `/Applications/TouchDesigner.app`; set it to the
  intended TouchDesigner installation. Its Python headers and framework must match.
- Set `OpenCV_DIR` to the folder containing `OpenCVConfig.cmake` if CMake cannot
  find OpenCV, or pass the installation prefix using `CMAKE_PREFIX_PATH`.
- `CMAKE_OSX_ARCHITECTURES=arm64` or `x86_64` selects the architecture. Use a fresh
  build directory and a matching OpenCV build; arm64 Homebrew libraries cannot
  link into an Intel bundle. Universal builds require universal dependencies.
- `CMAKE_OSX_DEPLOYMENT_TARGET` can select a different minimum with compatible
  dependencies. The source retains numeric formatting for targets before 13.3,
  but older macOS runtime support has not been validated.
- `OUTPUT_DIRECTORY` overrides the bundle output location for isolated checks.
- `TD_ADHOC_SIGN` defaults to ON, signing the local bundle without additional
  entitlements. This is not Developer ID signing or notarization.

The current bundle links Homebrew OpenCV dylibs under `/opt/homebrew/opt/opencv/lib`.
These libraries are **not embedded**. This is a local source-build workflow;
other machines need their own compatible OpenCV installation/build. Binary
redistribution requires a separate dependency-packaging/signing review.

Python uses the running host's framework, currently
`@rpath/Python.framework/Versions/3.11/Python`, with runtime search path
`@executable_path/../Frameworks`. No second Python runtime or Python headers are
copied into the bundle. See [source and attribution](#source-and-attribution).

Copy the built bundle into a `Plugins` folder beside your `.toe` file, or into
your TouchDesigner user Plugins directory, then restart TouchDesigner and create
**Segment UV** from the Custom OP menu. Replace the previous installed bundle
rather than keeping duplicate copies in the plugin search folders. Building does
not install or replace a plugin in your existing projects.

## TouchDesigner acceptance checklist

1. Install as a Custom OP and create Segment UV far from the network origin,
   initially with no input. Confirm one collapsed `_segments` DAT appears after
   frame end, with its viewer enabled and its top edge beneath the TOP.
2. Connect an asymmetric mask with several disconnected segments and a hole.
   Check the 5/9/15 column sets, bounding boxes, bottom-left centroids and counts.
   Check all five Methods, including repeatable Random values with Seed changes.
3. Compare the TOP with the previous bundle on your normal inputs. Change Common
   resolution, aspect, and pixel format; images should retain their established
   behavior and DAT geometry should stay at input resolution.
4. Change the input, then disconnect it and try an empty mask. Confirm headers
   remain and old rows disappear, including when switching DAT Detail without input.
5. Rename/copy nodes, copy a TOP alone, and save/reload. Each table should reference
   its own TOP without duplicates. Delete a table and confirm it stays deleted;
   repair should recreate it and reset its layout/viewer. Check unrelated name
   collisions and preservation of manual layout on ordinary reloads.
6. With a static input/constant parameters, let initialization finish, then inspect
   the TOP's cook count while viewing the DAT over several frames. It should not
   repeatedly cook. Animate the input and confirm the image/table update together.
   Use TouchDesigner's Performance Monitor to compare collapsed/expanded table
   costs at your typical segment count and resolution.

The four CTest suites cover independent measurements, legacy image parity,
Common packing/resizing, C++ host lifecycle/failure simulation, injected C++
allocation failures, and simulated-host docking. They do not substitute for the
above live Custom OP checks. Application feedback was positive; the individual
acceptance cases and live performance still require verification.

## Source and attribution

Version 0.2.0 preserves the original Segment UV image algorithm in
`SegmentEngine.cpp`; `tests/legacy.cpp` is a frozen test-only reference for image
parity and timing. The Info DAT interface and deferred docking bridge were
adapted from the maintained Convex Hull 0.3.0 implementation. All necessary code
is included locally; no other plugin is a build or runtime dependency.

The included SDK headers retain their Derivative copyright and Shared Use
License notices. Preserve those notices when sharing source. This snapshot uses
TOP API 12 and Common API 1. `TOPOutputHelper.h` is an unchanged local copy of the
maintained Common output helper (SHA-256
`fb110bc00dd67e8aefaa205508c9e7fbb2fbf2f79a5fd6f95e93cdff665b47b1`).

OpenCV is an external prerequisite; its code and libraries are not redistributed
here. The tested version is 4.12.0. TouchDesigner's Python headers and framework
are also external and are not redistributed in this source folder or bundle.
