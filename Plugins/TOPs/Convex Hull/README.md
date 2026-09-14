# Convex Hull

A TouchDesigner TOP that selects pixels with a threshold and renders their convex
hulls as binary masks, with an automatically docked Info DAT describing each hull.
Supports one enclosing hull, a hull per blob, and the largest blob, with
luminance, individual-channel, RGB, and RGBA processing. The extended measurement
and automatic-docking features are now part of the main Convex Hull plugin.

- Version: **0.3.0**. Label: **Convex Hull**; type: `Convexhull`.
- One non-empty 2D TOP input and one image output; synchronous CPU processing.
- C++17 and system threads for image analysis; TouchDesigner's bundled Python
  framework for deferred network setup. No OpenCV or separately installed Python
  runtime is required to use the plugin.
- Source targets macOS 12+ on arm64 and x86_64. The installed TouchDesigner
  validation target is 2025.33230. Native arm64 and x86_64 builds/tests passed
  (Intel tests under Rosetta). Physical Intel host loading, saved-node upgrades,
  and the full live acceptance checklist remain unverified.

## Parameters

| Page | Parameter (identifier) | Options / meaning | Default |
| --- | --- | --- | --- |
| Selection | Source (`Source`) | Luminance, Red, Green, Blue, Alpha, RGB Separate, RGBA Separate | Luminance |
| Selection | Threshold Mode (`Thresholdmode`) | Shared or Independent; enabled for RGB/RGBA | Shared |
| Selection | Threshold (`Threshold`) | Shared threshold, 0-1 | 0.5 |
| Selection | Red/Green/Blue/Alpha Threshold (`Thresholdr/g/b/a`) | Independent thresholds, 0-1; alpha enabled only for RGBA | 0.5 each |
| Selection | Threshold Direction (`Direction`) | Above: value > threshold; Below: value <= threshold | Above |
| Selection | Ignore Transparent Pixels (`Ignoretransparent`) | Require input alpha greater than Alpha Cutoff before selection | Off |
| Selection | Alpha Cutoff (`Alphacutoff`) | 0-1; enabled when transparency filtering is on | 0 |
| Hull | Hull Grouping (`Grouping`) | Global, Per Blob, Largest Blob | Global |
| Hull | Minimum Blob Area (`Minarea`) | Minimum original selected-pixel count per blob | 1 |
| Output | Output View (`View`) | Filled Hull, Outline, Selection Mask | Filled Hull |
| Output | Outline Width (`Outlinewidth`) | Integer input-pixel width; enabled for Outline | 1 |
| Output | Output Alpha (`Outputalpha`) | Opaque, Preserve Input, Result Mask; disabled in RGBA | Opaque |
| Output | DAT Detail (`Datdetail`) | Basic, Medium, Maximum (cumulative column sets) | Basic |
| Output | Create/Repair Hull DAT (`Repairhulldat`) | Create or repair the docked measurement Info DAT | Pulse |
| Common | Pixel Format (`format`) | Use Input or a supported storage format | Use Input |

Area and width accept integers from 1 to 2,147,483,647. Sliders show 1-1000 and
1-32 respectively; larger values can be entered. Thresholds and alpha cutoff
are clamped to 0-1. Switching modes preserves disabled parameter values.

## Selection and grouping

Luminance uses `0.2126 R + 0.7152 G + 0.0722 B` with the same float32 operation
order as Error Diffusion Dither. Input downloads use RGBA32Float and color-space
passthrough: no additional gamma conversion or unpremultiplication is applied.
Finite HDR and negative samples are compared directly. Nonfinite source samples
are excluded from both threshold directions. Nonfinite luminance is excluded.

Transparency filtering requires finite alpha strictly above Alpha Cutoff. This
filter applies to every selected source, including Alpha and RGBA's alpha plane.
It filters the input points; it does not clip a filled hull to the original alpha.

Pixels touching at edges or corners belong to the same blob (eight-neighbor
connectivity). Minimum Blob Area removes small blobs **before** hull calculation:

- **Global** encloses every retained pixel in a single hull per processed source.
- **Per Blob** calculates one hull per retained blob. Overlap is combined with a
  binary union, so it never increases values above 1.
- **Largest Blob** retains the blob with the most selected pixels, measured
  before filling. Ties select the first encountered from bottom-left, scanning
  left-to-right and then upward.

Selection Mask displays retained source pixels after area filtering and grouping
selection. It is useful for finding isolated pixels that stretch a global hull.

## Rendering and alpha

Luminance or an individual channel produces one mask repeated across RGB: white
on black. RGB Separate writes each independent mask into its corresponding
channel; the combined image is colored where hulls differ and white where they
overlap. RGBA Separate calculates a fourth mask for alpha and bypasses Output
Alpha. Its zero-alpha regions are transparent even when RGB contains a hull.

Opaque alpha is 1 everywhere. Preserve Input keeps finite input alpha clamped
to 0-1, replacing nonfinite alpha with 0. Result Mask uses `max(R, G, B)` of the
rendered result, so it follows Outline and Selection Mask views too.

Hull vertices are selected pixel centers. Filled output includes interior pixel
centers and a connected, one-pixel rasterization of the polygon boundary. The image
is a discrete raster hull. The Info DAT additionally reports geometric polygon
measurements; it does not expose a raw vertex list. It fills holes and
concavities. No selected pixels produces a zero mask; one selected pixel remains
a pixel; collinear selections form a connected line.

All calculated mask samples are exactly 0 or 1; no antialiasing is applied.
Thin lines use major-axis nearest-pixel interpolation, with halfway samples
rounded toward positive image coordinates. Wider outlines are round-capped
strokes centered on the hull boundary. Even widths have a half-pixel bias toward
positive X/Y so horizontal and vertical strokes occupy the requested width.
Outlines are clipped at image boundaries.

## Common page and image dimensions

Selection, area measurement, and rendering operate at the full input resolution.
Common Output Resolution performs final pixel-center nearest-neighbor resizing.
Common Output Aspect is honored. Outline Width and Minimum Blob Area therefore
remain measured in **input** pixels. Reducing output resolution does not reduce
analysis cost. Use an upstream Resolution TOP to reduce analysis cost deliberately.
Common Input Smoothness does not change this sampling policy; Viewer Smoothness
only affects viewing.

Pixel Format controls storage. Use Input explicitly resolves to the input's
format. Mono and Mono+Alpha store processed red, RG stores processed red/green,
and A stores the chosen output alpha. These storage reductions do not mix binary
RGB channels into new gray levels. RGBA8 is sufficient for hard masks; use
RGBA32Float when inspecting retained alpha numerically.

Supported storage: RGBA/BGRA 8-bit; RGBA 16-bit fixed/float and 32-bit float;
Mono, RG, A, and Mono+Alpha at 8/16-bit fixed and 16/32-bit float; RGB10A2 and
RGB11Float. Unknown formats fall back to BGRA8 with a warning. Masks remain
binary; preserved alpha is subject to the selected storage precision.

Missing, unsupported, invalid, or unallocatable input/output reports an operator
error. No partial result is uploaded; TouchDesigner may keep the last successful
texture visible until recovery. Reconnecting a valid input clears the error.

## Docked hull table

A new Custom OP schedules an Info DAT named `<node_name>_hulls` at frame end.
It starts collapsed with its viewer flag **on**. Opening the DAT-colored dock
icon reveals it directly beneath the TOP, left-aligned with a 30-network-unit
vertical gap. The layout uses the expanded viewer's height, not a fixed node
height or the network origin.

The DAT references `me.dock`, so it follows its owner through renames and copies;
its own name may remain unchanged when the TOP is renamed. Passive is disabled
for normal dependency-driven updates. There is no per-frame Python polling or
callback DAT. The data itself is supplied by the C++ Info DAT interface.

Existing matching docked tables are reused on instance initialization, copying,
and project reload. Tables created by the first extension prototype receive the corrected layout
and enabled viewer once on initialization. Later reloads preserve your manual
position and viewer changes. **Create/Repair Hull DAT** repairs references and
explicitly reapplies the below-TOP layout and enabled viewer, preserving whether
an existing table is expanded or collapsed. A conflicting unrelated node is
preserved and TouchDesigner chooses a free name. Deleting the table is respected
until instance initialization or the repair pulse.

Setup failures appear as TOP warnings; image processing and separately created
Info DATs remain usable. A table can also be created manually: point an Info DAT's
Operator parameter at this TOP, turn Passive off, and dock it.

**Output > DAT Detail** controls how much information appears. Basic is the
default, and each larger setting includes the preceding columns in order:

| Detail | Columns | Added data |
| --- | --- | --- |
| Basic | 6 | `id`, `source`, `u`, `v`, `width`, `height` |
| Medium | 10 | `left`, `top`, `right`, `bottom` |
| Maximum | 15 | `selected_pixels`, `blob_count`, `vertex_count`, `hull_area`, `hull_perimeter` |

- `id` is a zero-based, frame-local row index. `source` is `luma`, `r`, `g`, `b`,
  or `a`. IDs are not persistent tracking identities; there is no velocity output.
- `u`, `v` are the normalized bounding-box center, with `(0, 0)` at the bottom-left
  image edge and `(1, 1)` at the top-right. `width`, `height` are normalized box
  dimensions. Horizontal values divide by input width; vertical values divide by
  input height. The first pixel center is `(0.5 / input_width, 0.5 / input_height)`.
- Medium's edges use the same 0-1 coordinates. Right and top are exclusive pixel
  edges: `width = right - left`, `height = top - bottom`,
  `u = (left + right) / 2`, and `v = (bottom + top) / 2`.
- `selected_pixels` counts original selected pixels contributing to the hull
  after area/group filtering. `blob_count` counts retained connected blobs
  contributing to it and can exceed one in Global mode. `vertex_count` excludes
  a repeated closing vertex.
- `hull_area` and `hull_perimeter` remain in **square input pixels** and **input
  pixels**, respectively. They are geometric polygon measurements, not normalized
  dimensions or raster pixel counts. Counts are serialized with integer precision.

A solid `4 x 5` pixel rectangle in a `100 x 100` image has normalized width/height
`0.04`/`0.05`, polygon area `3 x 4 = 12`, selected-pixel count 20, and perimeter 14.
A single pixel has normalized size `1 / input_width` by `1 / input_height`, with
zero polygon area/perimeter. A line has zero area and twice its endpoint distance
as its closed-boundary perimeter.

The table describes the underlying hull at input resolution in every output
view, including Selection Mask and Outline. Thick outline pixels are excluded
from the bounds. Common-page resizing does not change the table. Multiply the
normalized box coordinates by output dimensions to map into the output image.
Rows use source order followed by component scan order (bottom to top, left to
right). Global and Largest Blob produce at most one row per source.

Changing detail changes the table's column count on the next cook. Empty results
and processing failures retain the selected headers with no stale rows. Only
selected columns are formatted; geometry/image processing is independent of the
detail setting. Table preparation must succeed before the texture is uploaded,
and callbacks read a completed owned snapshot.

The extended prototype replaced its initial 23-column schema: `hull_index` becomes
`id`, `u`/`v` replace pixel `x`/`y`, and box dimensions/edges are normalized. The
separate `_norm` and centroid columns are removed. Update any downstream DAT
column references that used the prototype names or pixel units.

## Performance and diagnostics

The engine thresholds horizontal pixel runs, labels connected runs, and builds
hulls from each retained row's outermost points. This is exact and does not
subsample the image. Buffers retain capacity for reuse. Removing the node releases
all buffers and joins all workers. Initialization requests one cook and schedules
docking once; subsequent image cooks remain demand-driven. Frames are completed
synchronously without a background queue or added frame latency.

RGB/RGBA work uses at most three persistent worker threads plus the cook thread.
Small images (fewer than 4096 pixels) and single-source jobs run inline. Workers
never call TouchDesigner APIs; allocation failures are transferred back to the
cook thread after all jobs finish.

Info CHOP exposes these fixed channels:

- `download_ms`, `process_ms`, `pack_ms`, `execute_ms`, `workers`, `retained_bytes`.
- For each prefix `luma`, `r`, `g`, `b`, `a`: `selected_pixels`, `retained_pixels`,
  `blobs`, `hulls`, `output_pixels`, and `coverage` (0-1).

Selected count is after threshold/transparency filtering but before area
filtering. Retained count and blob count are after area/group selection. Hull
count describes the selected grouping, including in Selection Mask view. Output
count/coverage measures the union of rendered pixels at input resolution.
Inactive sources report zero; a Red-to-monochrome result reports only `r` metrics.
Info CHOP values are floats, so very large integer counts lose integer precision.
Retained memory includes engine buffers and measurement records, excluding the
formatted DAT snapshot, host transfer buffers, and thread stacks. CPU timings do not include GPU completion of output upload.

1080p/60 is a performance target, not a guarantee. Dense noise can create very
large run/component tables. Historical image-only measurements do not establish
performance for the current DAT-enabled plugin. Use the included benchmark and
TouchDesigner Performance Monitor on your intended inputs.

## Upgrade from earlier versions

Version 0.3.0 replaces the original image-only implementation while retaining its
`Convexhull` operator type, Convex Hull label, and `ConvexHull.plugin` bundle name.
Existing original Convex Hull nodes retain that identity and gain the automatic
Info DAT, DAT Detail menu, and repair pulse. Input/image parameters and output
behavior are preserved. DAT Detail defaults to Basic. The build now requires
TouchDesigner's Python development headers/framework as documented below.

The temporary **Convex Hull Docked** prototype had a different operator type.
Nodes created with that prototype must be recreated as **Convex Hull**; changing
the installed plugin label does not rename saved node types. Remove the old
prototype bundle from your TouchDesigner Plugins search folders when replacing
those nodes. There is one maintained source implementation and build target.

## Build and install

Requirements: macOS, Apple's command-line developer tools (C++17 compiler and
macOS SDK), CMake 3.20 or newer, and a TouchDesigner installation containing its
Python framework and development headers. A Python 3 interpreter is also required
when building tests. The current binary targets TouchDesigner 2025.33230 with its
bundled Python 3.11.15; rebuild against the intended application when its Python
version changes. All required project headers, helpers, metadata, and tests are
included in this folder. No dependency download or workspace export is needed.

From this source folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The default architecture is the current Mac. For an Intel build from a clean
source copy, add `-DCMAKE_OSX_ARCHITECTURES=x86_64` to the configure command; for
Apple Silicon, use `-DCMAKE_OSX_ARCHITECTURES=arm64`. Running Intel tests on an
Apple Silicon Mac requires Rosetta. Physical Intel host validation is separate.

The bundle is `plugin/ConvexHull.plugin`; intermediate files stay in `build/`.
Both folders are ignored and must be excluded when publishing browsable source.
`TD_BUILD_TESTS` defaults to OFF. `OUTPUT_DIRECTORY` can redirect bundle output
for isolated builds. `TD_ADHOC_SIGN` defaults to ON on macOS and signs the bundle
with the local ad-hoc identity; no extra entitlements are needed.

`TD_APPLICATION` defaults to `/Applications/TouchDesigner.app`. For another
installation, configure with `-DTD_APPLICATION="/path/to/TouchDesigner.app"`. The
plugin links `@rpath/Python.framework/Versions/3.11/Python` for the current target,
with `@executable_path/../Frameworks` as its runtime search path. It uses the
framework supplied by the running host, and does not copy Python into the bundle.

Copy the bundle into a `Plugins` folder beside your `.toe` project and restart
TouchDesigner, then create **Convex Hull** from the Custom OP menu.
Automatic docking requires installation as a Custom OP; loading the bundle into
a generic CPlusPlus TOP supports image/Info DAT testing but does not provide
the Custom OP Python integration. It can also be installed in the TouchDesigner user Plugins
directory. See [Derivative's Custom Operators installation guidance](https://docs.derivative.ca/Custom_Operators).

An ad-hoc signature is for local builds, not Developer ID signing/notarization.
Downloaded binary distribution remains a separate release step. This project
does not install, stage, or publish the generated bundle automatically.

## Tests and host validation

The CTest suite checks the docking helper with a simulated host, independently
verifies geometry/DAT values and normalization, and compares against independent gift-wrapping and flood-fill
references, checks storage and resizing, stresses engine lifetime, and injects
allocation failures on both the caller and workers. Run the standalone benchmark:

```sh
./build/HullBenchmark 1920 1080 9
```

It writes CSV for four fixtures, seven sources, and three grouping modes, with
two warmups and nine measured frames per case. It measures CPU analysis plus
RGBA8 packing; GPU transfers and TouchDesigner scheduling are excluded. The p95
of nine samples is the slowest sample. Use more samples for a steadier tail estimate.

For the inherited image/parameter suite (which uses a generic CPlusPlus TOP and
does not test automatic docking), replace the path below and run in its Python
Textport. The script uses TouchDesigner's bundled NumPy and creates a uniquely
named test Base COMP under `/project1`; it does not save or replace a project.

```python
from pathlib import Path
folder = Path('/absolute/path/to/convex-hull')
test = folder / 'tests/touchdesigner_validate.py'
exec(compile(test.read_text(), str(test), 'exec'))
run_validation(str(folder / 'plugin/ConvexHull.plugin'), benchmark=True)
```

The script checks source/group/view combinations, alpha, independent thresholds,
parameter enabling, Common formats/resolution/aspect, reconnecting, and creation
and deletion. It leaves its network for inspection and writes a JSON report under
the system temporary directory. Normal plugin trust prompts may require action.

The optional host benchmark records forced-cook wall time and `cpuCookTime`,
then separately records cook plus blocking output readback. Readback waits for
output but adds a transfer; CPU cook timing alone does not establish GPU frame
time. See [Performance Monitor](https://docs.derivative.ca/Performance_Monitor)
and [NumPy/TOP downloads](https://docs.derivative.ca/NumPy). Also inspect live 1080p
input with Performance Monitor and confirm static input does not recook merely
because the timeline advances. A successful standalone build cannot replace
these host checks.

## Source and attribution

The convex hull engine and measurement/docking extension are developed for this
plugin. No OpenCV code is included or linked. The SDK headers and Common output
helper were adapted from the maintained Error Diffusion Dither source. Their
original Derivative copyright and license headers are preserved in each SDK file;
retain those notices when sharing source. The SDK snapshot uses TOP API 12 and
Common API 1. `TOPOutputHelper.h` retains the maintained helper's SHA-256
`fb110bc00dd67e8aefaa205508c9e7fbb2fbf2f79a5fd6f95e93cdff665b47b1`.

TouchDesigner supplies Python development headers at build time and its Python
framework at runtime. Neither is redistributed in this source folder or bundle.
All project-specific source, helper copies, metadata and tests are included here.

## TouchDesigner acceptance checklist

1. Install the built bundle as a Custom OP and create **Convex Hull**.
   Confirm one collapsed `_hulls` Info DAT appears after frame end, including
   when the TOP initially has no input. Open it: confirm its viewer is on and it
   sits directly beneath the TOP, including far from the network origin.
2. Try a known rectangle, multiple blobs, and RGB/RGBA Separate. Check source
   labels and normalized `u`, `v`, width, and height. Switch Output > DAT Detail
   through Basic/Medium/Maximum and confirm 6/10/15 columns with the documented
   order and units. Compare the image with the original Convex Hull.
3. Switch Global/Per Blob/Largest Blob, minimum area, Filled/Outline/Selection,
   and Common output resolution. Confirm geometry follows grouping/filtering
   and is unchanged by output view, outline width, or final resizing.
4. Change the input, then disconnect it and try an empty mask. Confirm the DAT
   updates through its normal dependency and stale rows disappear on errors.
   Change DAT Detail with an empty/erroring TOP and check the selected headers.
5. Rename and duplicate the TOP with its docked DAT; also copy the TOP alone.
   Each resulting DAT must reference its own TOP. Save/reload a test project
   and reinitialize an instance; matching tables must not be duplicated.
6. Delete the DAT, let the timeline run, and confirm it stays deleted. Press
   **Create/Repair Hull DAT** and confirm it returns. Try an unrelated node
   with the expected name and confirm it is preserved. Reposition the DAT and
   disable its viewer, then pulse repair to restore the default layout and viewer.
   Normal reloads should preserve edits after the one-time prototype layout upgrade.
7. Check the warning indicator if setup fails, test repair, and inspect live
   cook time. Application-level results are pending your TouchDesigner test.
