# Convex Hull validation

Date: 2026-09-07. Version 0.1.0. Release compiler: Apple Clang 21.0.0.21000101,
macOS 26.5 SDK, macOS 12.0 deployment target. Installed TouchDesigner: 2025.33230.

## Completed checks

- Native arm64 Release build and both CTest suites passed.
- 29,094 independent full-image/statistics comparisons passed. These include
  exhaustive 3x3 masks, seven sources, all grouping/view/alpha combinations,
  shared/independent thresholds, threshold ties/direction, transparency filtering,
  nonfinite/HDR input, area filtering, and outlines from 1 to 100 pixels.
- Hull vertices match independent gift wrapping for every 3x3 point set. Mask
  selection/grouping uses an independent pixel flood-fill oracle. Rendering is
  checked against pixel-center half-plane and point-to-segment references, plus
  the documented connected boundary rounding rule.
- Degenerates, narrow/odd images, ring holes and overlapping hulls, area/size ties,
  border clipping, padded input rows, and post-render nearest resizing passed.
- All 23 SDK storage formats passed bounds checks and independent numeric decode;
  preserved-alpha byte rounding and mono channel mapping passed.
- Serial/parallel parity, mode/dimension changes, simultaneous engine instances,
  buffer-capacity stability and repeated construction/destruction passed.
- 118 injected allocation failures propagated safely, followed by successful
  processing on the same engine, including partially created worker pools.
- AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer passed both
  reference/lifecycle and allocation-failure suites.
- Host script syntax passed. Its independent Python reference passed basic
  binary/channel invariants on 4,608 small-mask cases outside TouchDesigner.
  This does not validate the host-only calls or the plugin in the application.
- Native bundle ad-hoc signature, metadata and required TOP exports passed.
  Linked libraries are only libc++ and libSystem; no external dylib paths.

## Source isolation and architectures

Two ordinary source-folder copies, excluding build/plugin/cache output, were
created outside the workspace. Each was configured and built from its own folder
using the README commands and only included files/documented compiler tools.

- Native arm64 configure/build and both CTest suites passed.
- x86_64 configure/build with `CMAKE_OSX_ARCHITECTURES=x86_64` and both CTest
  suites passed; Intel executables ran under Rosetta on this Apple Silicon Mac.
- Both bundles pass `codesign --verify --strict`, contain the expected Mach-O
  architecture, and link only system libc++ and libSystem.
- Source-folder Markdown links resolve locally; neither copy contains symlinks
  or dependencies on parent/sibling workspace files.
- The workspace's source-local bundle remains arm64. Physical Intel Mac loading
  and performance in TouchDesigner remain unverified.

The source is distributed as a normal browsable folder. Generated local bundles
and build directories remain ignored; no source archive/export step is required.

## 1080p CPU benchmark

The local workstation is the Apple Silicon Mac used for this workspace. These
are synthetic input measurements, not a live camera/video benchmark. Each case
uses two warmups and nine measured frames, Filled Hull, Shared threshold 0.5,
Above, Minimum Blob Area 1, no transparency filtering, and opaque output alpha
(RGBA mode calculates alpha independently). Downloaded input is represented as
RGBA32Float; output packing is RGBA8. GPU transfers and host scheduling are excluded.

Fixtures: all pixels selected; repeated rectangles shifted per channel; 0.5%
independent sparse foreground; and 50% independent random foreground. The CSV
contains all seven sources and all three grouping modes:
[raw benchmark results](tests/benchmark-1080p.csv).

Median analysis + packing time, milliseconds:

| Fixture | Source | Global | Per Blob | Largest Blob |
| --- | --- | ---: | ---: | ---: |
| solid | luminance | 5.10 | 4.18 | 4.22 |
| solid | rgb | 4.48 | 4.48 | 4.50 |
| solid | rgba | 4.71 | 4.76 | 4.75 |
| shapes | luminance | 4.59 | 4.74 | 4.51 |
| shapes | rgb | 4.57 | 4.69 | 4.48 |
| shapes | rgba | 4.88 | 5.03 | 4.78 |
| sparse | luminance | 4.90 | 4.86 | 4.65 |
| sparse | rgb | 4.96 | 5.06 | 4.72 |
| sparse | rgba | 5.21 | 5.30 | 5.05 |
| noise | luminance | 26.03 | 26.73 | 25.93 |
| noise | rgb | 24.37 | 25.69 | 24.22 |
| noise | rgba | 25.11 | 26.80 | 25.38 |

Ordinary/sparse fixture medians across all sources/groupings were
4.15-5.30 ms; dense random noise was
22.85-26.80 ms. The dense-noise cases exceed the 16.67 ms
budget before GPU transfers, so 1080p/60 is **not guaranteed**. Filtering small
blobs does not eliminate the cost of finding those blobs. No sampling or geometry
approximation was used to meet a timing target.

Maximum retained engine memory across these cases was
72.07 MiB, excluding host
transfer buffers and worker stacks. Plane buffers retain capacity after a large
or noisy image until the node is destroyed. Runtime measurements vary with
hardware, image content, other nodes, output format and output size.

The binary packing path was optimized after an initial profile showed about
18 ms in generic float output packing. Per-blob hull construction also collapses
same-row endpoints before sorting; this preserves geometry and avoids sorting
many redundant points in a large noisy component.

## Pending TouchDesigner checks

Computer Use access to TouchDesigner was denied by the tool in this session.
No plugin load, native-node parameter check, live project modification, or
full host/GPU cook benchmark was performed. The installed 2025.33230 build is a
validation target, not a verified host compatibility claim.

Run the included host suite using the [README instructions](README.md#tests-and-host-validation).
It checks plugin loading, image results, parameter enabling, Common output,
reconnect and creation/deletion; its optional benchmark separates CPU cook wall
time from cook plus forced output readback. Check static-input recooking and
moving 1080p input with Performance Monitor as well.

Older macOS/TouchDesigner releases and physical Intel Mac loading/performance
remain unverified. No compiled release was staged or published.
