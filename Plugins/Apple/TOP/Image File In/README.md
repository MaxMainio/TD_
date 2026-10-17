# Apple Image In

An Apple Silicon TouchDesigner TOP that reads local images with Apple's Image I/O,
outputs color and available auxiliary images simultaneously, and docks a metadata
Info DAT. Version 0.2.0; label **Apple Image In**; type `Appleimagefilein`;
bundle `AppleImageFileIn.plugin`. The existing internal type and bundle name
are retained so saved projects remain compatible with the shorter display name.

Requires macOS 14+ and TouchDesigner 2025.33230 with its Python 3.11 runtime.
ISO gain maps require macOS 15+. Older macOS releases are deployment targets,
not separately runtime-tested platforms.

## Use

Create **Apple Image In** from the Custom TOP list and choose **File**.
TouchDesigner file expressions and project-relative paths are supported.
Only local filesystem files are accepted. No extension allowlist is applied:
Image I/O decides whether the current macOS can read the file.

Requested format families include HEIC/HEIF, CIFF/Canon CRW, DNG, GIF, JPEG/JFIF,
PNG, TGA, TIFF and Photoshop composites with Apple-exposed 8BIM metadata.
Camera model, encoding and macOS decoder support vary. This is still-image/page
selection; animation playback, image sequences, RAW development controls and
Photoshop layer reconstruction are not implemented.

| Parameter | Default | Behavior |
| --- | --- | --- |
| File | Empty | One local image or image container |
| Image | Primary Image | Actual zero-based images/pages, dimensions and available primary/stereo roles |
| Decode | Auto | Auto and HDR request Apple's HDR rendering when available; SDR requests SDR rendering |
| Auto Refresh | On | Check file identity, size and timestamps on the worker |
| Refresh | Pulse | Reread even when the file's stat identity is unchanged |
| Auxiliary Outputs | Automatic | Prepare expanded, connected or natively referenced helpers; All Available keeps every available buffer ready |
| Check Interval (s) | 1 | File checks, clamped to 0.1-86400 seconds |
| Create/Repair Docked Outputs | Pulse | Recreate missing owned helpers and restore references/layout |

Parameter changes and completed background reads update while the timeline is
paused. A small host-thread callback uses TouchDesigner's independent clock;
it checks parameters and worker completion every display frame (approximately 16 ms). File I/O and image
decoding remain on a worker. Static images are not uploaded every frame. Common
changes repack only buffers whose output size or storage actually changes.
Resolution values are used by Custom Resolution, Fit Resolution and Limit
Resolution. TouchDesigner 2025.33230 keeps these built-in fields editable in
other modes for this source TOP; its plugin APIs cannot override their enable
state. Their values do not affect Use Input or the fixed scale modes.

## Color and orientation

The selected image is decoded at full resolution. Image I/O reads its embedded
profile; Core Image converts to extended linear sRGB with premultiplied alpha.
EXIF orientation is applied once. Common resolution controls set the main color
size and cap auxiliary output dimensions as described below.

In a color-managed project, the plugin uploads linear sRGB and tells TouchDesigner
to convert it to the project's working color space. In a **Passthrough** project,
TouchDesigner skips that conversion, so the plugin supplies extended sRGB-encoded
values for the normal image display. This avoids displaying linear values as
gamma-encoded colors. The sRGB transfer is applied to straight color and alpha
is then premultiplied again. Floating-point values outside 0-1 are retained;
their numerical representation differs between linear and sRGB-encoded outputs.

New instances default to RGBA16Float. Common also offers RGBA32Float and the
standard supported formats. Integer formats clip HDR/out-of-gamut values. Auto
preserves HDR when Apple's decoder exposes it; it does not invent HDR or depth.
Use SDR when an SDR rendition of an HDR image is wanted. Image I/O's SDR request
does not guarantee that arbitrary floating-point TIFF data will be tone-mapped.

## Docked outputs

Expand the initially collapsed helpers, stacked in a column below the TOP, to find `<node>_metadata` and Render Select
TOPs for **successfully decoded** auxiliary images. Each kind has a permanent
buffer index. Changing files cannot silently redirect a connection to another
kind.

| Index | Kind | Storage |
| --- | --- | --- |
| 0 | Color | Common format, initially RGBA16Float |
| 1 | Depth | Mono32Float, native depth values |
| 2 | Disparity | Mono32Float, native disparity values |
| 3 | Portrait matte | Mono32Float |
| 4 | Skin | Mono32Float |
| 5 | Hair | Mono32Float |
| 6 | Teeth | Mono32Float |
| 7 | Glasses | Mono32Float |
| 8 | Sky | Mono32Float |
| 9 | Apple HDR gain map | Native mono/RG/RGB/RGBA components |
| 10 | ISO gain map | Native mono/RGB components; macOS 15+ |

Auxiliaries are probed for the **selected image** and align their orientation
with color. The main TOP's resolved width and height are maximums for every
auxiliary buffer. Each axis is capped independently at the smaller of its native
size and the main output size; smaller maps are never upscaled. For example,
with 4000 x 3000 color, a 2000 x 1500 matte and a 500 x 375 disparity map,
Quarter produces 1000 x 750 color and matte while disparity stays 500 x 375.
Downsampling uses bilinear sampling of the cached native pixels. Increasing the
cap restores native detail from that cache without cumulative resampling or a
file reread. Metadata records both capped `width`/`height` and
`native_width`/`native_height`, including while a map is inactive.
Data maps bypass display-color conversion and retain their native float formats.
Depth/disparity values are not normalized or converted into each other. Apple
reports nominal units of meters / inverse meters; relative-accuracy maps are
not calibrated distance measurements. The DAT records accuracy, quality,
filtering and available camera calibration. Integer matte/gain samples become
their normalized floating-point sample values; gain-map metadata describes how
to interpret them, and the gain-map output itself is not an HDR color image.

Multi-component auxiliary buffers use RG32Float or RGBA32Float. TouchDesigner's
CPU upload API has no three-component float format, so RGB gains are padded with
alpha 1; `source_channels` remains 3. Planar RGB gain maps are unpacked through
Core Image with color management disabled. Unsupported or damaged auxiliary
rasters produce a partial-result warning and no new selector.

Unavailable unused plugin-owned selectors are removed. Wired or natively referenced ones stay at the
same index with black output, an unavailable comment and a parent warning.
They resume when that kind returns. Unrelated nodes are never removed. Deleting
an owned helper intentionally keeps it absent for the current file until repair
or instance reload. References use `me.dock`, so they follow rename and copy.
Repair preserves the user's expanded/collapsed choice after initial setup.

### Cooking and idle helpers

**Automatic** is the default. A collapsed, unused helper has a cleared placeholder
buffer; the parent skips its float conversion, orientation copy and GPU upload.
Its validated availability, native dimensions and permanent index remain in the
metadata. Expanding or connecting a helper resumes it on the independent host
clock, including with the timeline paused. The worker prepares just that map
from cached native data, without rereading or copying color. Prepared maps are
cached for the current source snapshot, so subsequent resumption only uploads
that map. Collapse/disconnect clears its buffer once and stops further work.
The plugin does not lock, bypass or force-cook idle helpers.

Native Select TOP references and direct Render Select buffer references also
count as use; these references are checked four times per second. Floating viewers, expressions and arbitrary scripts cannot be reliably
identified by this host API: setup cooks look like consumer requests. For these
uses, choose **Auxiliary Outputs > All Available** before opening the viewer or
reading pixels. This also supports direct buffer access outside Render Select.
Wait for `outputs.<kind>.active` to become `true` before a synchronous script read.
The metadata distinguishes `inactive`, `loading` and `ready` states; an inactive
map never presents old pixels from a different source.

Every file read still validates native auxiliary payloads to discover which
helpers can be offered safely. Apple's Image I/O may decompress those payloads
as part of discovery; AVFoundation validates/orients depth, and planar ISO RGB
gain maps require an initial Core Image unpack to verify them. This optimization
removes avoidable preparation and uploads, not all initial file-decoding cost.
Native auxiliary cache storage is capped at 1 GiB in addition to the 1 GiB
potential float-image limit. Refresh replaces the cache; inactive maps remain
unprepared in the replacement snapshot.

Automatic helpers require the registered Custom OP. A generic CPlusPlus TOP
does not expose its Python callbacks.

## Metadata and diagnostics

The ordinary Info DAT reads the TOP's cached snapshot. Its four columns are
`key`, `value`, `group`, `source`, matching MetaData In's table convention.
Keys are qualified and sorted, nested name punctuation is escaped, arrays use
zero-based brackets, and repeated keys receive `#2`, `#3`, etc. Limits are 20,000
rows, 16 MiB of cell text and 64 KiB per cell. Truncation is reported. Binary
payloads are summarized by size; pixel bytes are never put in cells.

Rows include filesystem stat information, container and image properties,
Apple-exposed EXIF/IPTC/XMP/GPS and Photoshop metadata, image inventory,
auxiliary descriptors/calibration, output mappings and decode diagnostics.
Coverage is what Apple's APIs expose; no ExifTool or ffprobe is bundled.

Color, auxiliaries and table are published together. Source/image/decode changes
clear the previous snapshot while loading; automatic and manual rereads retain
the last complete snapshot until a replacement arrives. Errors clear stale
content; an auxiliary error can retain valid color with a warning. If the host
cannot allocate output textures, clearing can also fail and a node error reports
that condition.

The popup reports state, selected file/image, size, read count/duration and
warnings. Info CHOP channels are `reading`, `images`, `image_index`, `width`,
`height`, `read_ms`, `has_<kind>` and `active_<kind>` for the ten auxiliary kinds,
plus `auxiliary_preparations`, `auxiliary_uploads` and `auxiliary_prepare_ms`.
Preparations count float maps materialized in the current source snapshot; uploads
count nonempty auxiliary texture submissions over the instance lifetime. Dimensions,
strides and allocation arithmetic are checked; decoded results and individual
output allocations are limited to 1 GiB, with dimensions up to 65536.
Cancellation discards superseded results between Apple API calls. An Apple
decoder already inside a native call must return before worker shutdown finishes.

## Build and install

Prerequisites: Apple Silicon, Xcode Command Line Tools with macOS 15+ SDK,
CMake 3.20+, C++17/Objective-C++ compiler, and an installed TouchDesigner
2025.33230 application containing Python.framework and its development headers.
A Python 3 interpreter is also needed for tests. No third-party dependency
download is needed.

From this standalone source folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTD_APPLICATION=/Applications/TouchDesigner.app
cmake --build build --parallel
```

The installable result is `plugin/AppleImageFileIn.plugin`; intermediates stay
in `build/`. `-DOUTPUT_DIRECTORY=/absolute/output/path` redirects the bundle.
Ad-hoc signing is enabled by default (`-DTD_ADHOC_SIGN=OFF` disables it).

The plugin links system ImageIO, CoreImage, CoreGraphics, AVFoundation, CoreVideo,
Foundation and CoreFoundation frameworks. Python is supplied by TouchDesigner
through `@rpath` and `@executable_path/../Frameworks`; it is not embedded. Rebuild
for a different Python ABI. No extra entitlements are required. Ad-hoc signing
does not provide Developer ID signing or notarization.

Copy the bundle to a `Plugins/` folder next to your `.toe`, or to
`~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`.
Restart TouchDesigner and create the Custom TOP. Keep only one installed copy
of this operator version. See Derivative's
[Custom Operators guide](https://docs.derivative.ca/Custom_Operators) and
[Render Select TOP](https://docs.derivative.ca/Render_Select_TOP).

## Validation

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
otool -L plugin/AppleImageFileIn.plugin/Contents/MacOS/AppleImageFileIn
codesign --verify --deep --strict plugin/AppleImageFileIn.plugin
```

Core Image tests need normal macOS graphics-service access. A restricted sandbox
may prevent graphics initialization. Fixtures are generated into `build/fixtures`.
The suites cover formats, orientations, color/alpha/HDR precision, collections,
disparity/mattes/gain maps, caching, cancellation, allocation failures, host buffer
publication and simulated helper lifecycle. Live validation is separate: load
`tests/touchdesigner_validate.py` in the Textport and call
`run_validation('/absolute/build/fixtures', '/absolute/validation-output')`.
It pauses and restores the root timeline and creates a dedicated test component.
After saving/reloading a test project, call `validate_reloaded` with that same
output directory. The harness writes a JSON report; it never saves a project
itself. For demand-specific live checks, execute `tests/touchdesigner_demand.py` and call
`run_demand_validation` with the same two directory arguments. It checks idle
preparation, paused resume/suspend, references, unavailable outputs and refresh.
For Common resolution checks, execute `tests/touchdesigner_resolution.py` and
call `run_resolution_validation` with those directory arguments. It records
host-managed enable states and verifies mixed native map sizes, caps, cache
reuse and paused changes.
The main harness includes a midtone comparison with Movie File In and paused file
changes, Common controls, missing/wired outputs, repair, rename and copy.

Generated fixtures do not establish acceptance of every camera variant.
Real Canon CRW/CIFF, camera DNG/ProRAW, iPhone portrait/calibration, camera HDR and
spatial/stereo HEIC variants remain unvalidated until actual fixtures are supplied.
Use `tests/camera-fixtures.json` to record provenance and expected observations;
private photographs are not distributed. macOS 14/15 and other TouchDesigner
builds have not been tested on separate installations.

## Source and attribution

All project-specific sources, helper copies and tests are included locally.
The host bridge adapts this workspace's Face Detection deferred docking and
MetaData In path/table patterns, without changing either plugin.
`TOPOutputHelper.h` is the maintained helper snapshot copied on 2026-10-17;
host test stubs adapt Face Detection. SDK headers `TOP_CPlusPlusBase.h`,
`CPlusPlus_Common.h` and `BitMaskFlags.h` were copied unchanged from
TouchDesigner 2025.33230's CPUMemoryTOP sample (TOP API 12, Common API 3).
Their Derivative Shared Use License headers are retained and apply to those
files; they may only be used with TouchDesigner under its license terms.
TouchDesigner's Python and Apple's SDK/frameworks are installed prerequisites.

Apple references: [CGImageSource](https://developer.apple.com/documentation/imageio/cgimagesource),
[HEIC properties](https://developer.apple.com/documentation/imageio/heic-image-properties),
[AVDepthData](https://developer.apple.com/documentation/avfoundation/avdepthdata).
