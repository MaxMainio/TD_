# Convex Hull

A TouchDesigner TOP that selects pixels with a threshold and renders their convex
hulls as binary masks. Supports one enclosing hull, a hull per blob, and the
largest blob, with luminance, individual-channel, RGB, and RGBA processing.

- Label: **Convex Hull**; type: `Convexhull` (compatibility-sensitive).
- One non-empty 2D TOP input and one image output; synchronous CPU processing.
- C++17 and system threads. No OpenCV, Python runtime, Metal, or other third-party
  libraries are required by the plugin.
- Source targets macOS 12+ on arm64 and x86_64. The installed TouchDesigner
  validation target is 2025.33230. See [validation status](VALIDATION.md) for what
  was actually tested; compilation does not establish host compatibility.

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
centers and a connected, one-pixel rasterization of the polygon boundary. This
is a discrete raster hull, not a polygon/vertex output. It fills holes and
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

## Performance and diagnostics

The engine thresholds horizontal pixel runs, labels connected runs, and builds
hulls from each retained row's outermost points. This is exact and does not
subsample the image. Buffers retain capacity for reuse. Removing the node releases
all buffers and joins all workers. Only requested cooks run; frames are completed
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
Retained memory includes engine buffers, excluding host transfer buffers and
thread stacks. CPU timings do not include GPU completion of output upload.

1080p/60 is a performance target, not a guarantee. Dense noise can create very
large run/component tables. See [measured results and limitations](VALIDATION.md).

## Build and install

Requirements: macOS, Apple's command-line developer tools (C++17 compiler and
macOS SDK), CMake 3.20 or newer, and a compatible TouchDesigner installation to
load the plugin. All required project headers, helpers, metadata, and tests are
included in this folder. No dependency download or workspace export is needed.

From this source folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

The bundle is `plugin/ConvexHull.plugin`; intermediate files stay in `build/`.
Both folders are ignored and must be excluded when publishing browsable source.
`TD_BUILD_TESTS` defaults to OFF. `OUTPUT_DIRECTORY` can redirect bundle output
for isolated builds. `TD_ADHOC_SIGN` defaults to ON on macOS and signs the bundle
with the local ad-hoc identity; no entitlements or third-party dylibs are needed.

Load the bundle using a CPlusPlus TOP's Plugin parameter, or copy it into a
`Plugins` folder beside your `.toe` project and restart TouchDesigner to discover
the custom operator. It can also be installed in the TouchDesigner user Plugins
directory. See [Derivative's Custom Operators installation guidance](https://docs.derivative.ca/Custom_Operators).

An ad-hoc signature is for local builds, not Developer ID signing/notarization.
Downloaded binary distribution remains a separate release step. This project
does not install, stage, or publish the generated bundle automatically.

## Tests and host validation

The CTest suite compares against independent gift-wrapping and flood-fill
references, checks storage and resizing, stresses engine lifetime, and injects
allocation failures on both the caller and workers. Run the standalone benchmark:

```sh
./build/HullBenchmark 1920 1080 9
```

It writes CSV for four fixtures, seven sources, and three grouping modes, with
two warmups and nine measured frames per case. It measures CPU analysis plus
RGBA8 packing; GPU transfers and TouchDesigner scheduling are excluded. The p95
of nine samples is the slowest sample. Use more samples for a steadier tail estimate.

For actual TouchDesigner checks, replace the path below and run in its Python
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

See [source and license notices](NOTICES.md) for the origins of included helpers.
