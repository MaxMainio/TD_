# Apple Face Detection

A TouchDesigner TOP that detects faces with Apple Vision, outputs a filled face
mask, and automatically docks an Info DAT containing the detected 2D landmarks.

- Version **0.1.0**; label **Apple Face Detection**; type `Applefacedetection`.
- One 2D TOP input; one mask TOP output and a docked landmarks Info DAT.
- Apple Silicon, macOS 12+; the chosen TouchDesigner build may require newer macOS.
- Synchronous processing: the mask and table describe the same input cook.
- Uses Apple's built-in face model. No model download, OpenCV, or third-party
  machine-learning runtime is needed. Vision chooses its compute devices; this
  plugin does not promise Neural Engine-only execution.

## Use

Install the bundle as described below, then create **Apple Face Detection** from
TouchDesigner's Custom TOP operators and connect an image or video TOP.
The default output is white filled face ovals on black, with the mask also in
alpha when the output format has alpha. Expand the TOP's dock to see its
`<node_name>_landmarks` Info DAT.

The oval is inscribed in each face bounding box and stays axis-aligned. It is a
geometric face-region mask, not skin segmentation, a hair/head matte, or an
occlusion-aware cutout. Profile faces, small faces, lighting and occlusions can
affect detection. A frame with no accepted faces produces a black mask and a
header-only DAT.

## Parameters

| Parameter | Default | Behavior |
| --- | --- | --- |
| Mask Shape (`Maskshape`) | Face Oval | Face Oval, Landmark Hull, or Bounding Box |
| Faces (`Faces`) | All Faces | Combine all accepted faces, or use only the largest bounding box |
| Minimum Confidence (`Minconfidence`) | 0.5 | Keep faces with detection confidence at or above this value, from 0 to 1 |
| Create/Repair Landmarks DAT (`Repairlandmarksdat`) | Pulse | Create a missing DAT or repair its docking, reference and layout |

Landmark Hull fills the convex hull of Vision's full landmark set. It does not
add inferred forehead points. If a face has fewer than three usable,
non-collinear landmark points, this mode falls back to an oval. Bounding Box
fills the detected rectangle. All masks are clipped to the image, have four
coverage samples per output pixel, and combine overlapping faces by maximum.

Common output resolution, aspect and supported pixel formats are respected.
Masks are rendered at the requested output resolution. `Use Input` uses the
input's storage format; choosing a monochrome format saves output storage.
Every available component receives the mask, including alpha. RGB, RGBA, mono,
RG, alpha-only, mono+alpha, RGB10A2 and RGB11 float formats are supported.
Unsupported formats fall back to BGRA8. Mask values bypass color conversion.
The input is downloaded as upright sRGB BGRA8 for Vision; alpha is not a face
selection filter.

## Landmarks DAT

Each row describes one point in a named facial region. The first row contains:

| Column | Meaning |
| --- | --- |
| `face` | Zero-based index among accepted faces in this frame |
| `region` | Vision facial region name, listed below |
| `point` | Zero-based point index within that region, in Vision's order |
| `u`, `v` | Full-image normalized coordinates; origin at the bottom-left |
| `confidence` | Face detection confidence, repeated per row; not per-point accuracy |
| `left`, `bottom`, `width`, `height` | Face bounding box in normalized image coordinates |

Regions are `face_contour`, `left_eye`, `right_eye`, `left_eyebrow`,
`right_eyebrow`, `nose`, `nose_crest`, `median_line`, `outer_lips`, `inner_lips`,
`left_pupil` and `right_pupil`. Names and point order follow Vision. Missing
regions have no rows. Named regions can share points; the table does not append
an additional duplicate `all_points` region.

The request pins Vision revision 3 and the 76-point constellation. This does
not mean every face yields 76 DAT rows: rows enumerate the available named
regions. Coordinates are converted from face-local to full-image space using
`u = left + local_x * width` and `v = bottom + local_y * height`. They do not
change when Common output resolution changes. Points near image edges are not
clamped, so partially visible faces can contain values outside 0-1. For image
coordinates measured from the top-left, use `x = u * image_width` and
`y = (1-v) * image_height`.

Faces are sorted by left edge, then bottom edge, width and height. The index is
local to the current frame, not a persistent tracking or identity ID. Largest
Face selection filters both the mask and DAT. A detected face without landmarks
can still produce a mask but contributes no landmark rows.

The DAT is an ordinary Info DAT reading cached C++ output. Its `op` expression
is `me.dock` and Passive is off, allowing it to request a cook of its TOP.
Network setup is deferred to frame end and uses the TOP's node ID. Rename,
copy and saved-node reuse follow the dock parent; unrelated DATs are preserved.
Deleting the DAT leaves it deleted until the repair pulse or instance reload.
Automatic docking requires the installed Custom OP; a generic CPlusPlus TOP
does not expose the Python callback used by this feature.

Info CHOP channels are `faces`, `landmark_rows` and `detection_ms`. Failures clear
the landmark table, report a node error and attempt to upload black. If the
host cannot allocate or accept an output buffer, it may retain its previous
texture while showing that error. Reading cached DAT values does not run a
second Vision request.

## Build

Requirements:

- An Apple Silicon Mac, Xcode Command Line Tools with a macOS 12+ SDK, CMake 3.20+,
  and a C++17/Objective-C++ compiler.
- An installed TouchDesigner app containing Python.framework and its development
  headers. Configure against the app that will load the plugin.
- A Python 3 interpreter only when running the docking tests.

From this standalone source folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTD_APPLICATION=/Applications/TouchDesigner.app
cmake --build build --parallel
```

The result is `plugin/AppleFaceDetection.plugin`. Intermediates stay in `build/`.
Use `-DOUTPUT_DIRECTORY=/absolute/output/path` to place bundles elsewhere.
Ad-hoc signing is on by default; `-DTD_ADHOC_SIGN=OFF` disables that local signing
step. Distribution notarization is separate and is not performed by this build.

The bundle links Apple's Vision, CoreVideo, Foundation and CoreFoundation
frameworks and TouchDesigner's Python through `@rpath`. The runtime search path
is `@executable_path/../Frameworks`, so Python is supplied by the running
TouchDesigner app. Python is not embedded. Rebuild for a host with a different
Python ABI. No extra entitlements or embedded third-party dylibs are required.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTD_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
otool -L plugin/AppleFaceDetection.plugin/Contents/MacOS/AppleFaceDetection
codesign --verify --deep --strict plugin/AppleFaceDetection.plugin
```

The Vision smoke test needs access to normal macOS graphics services; a
restricted sandbox can block CoreVideo/IOSurface allocation. Tests cover mask
geometry, coordinate conversion, selection, table clearing, host download/upload
failures, Common output, simulated docking lifecycle, and real blank-image Vision
requests with pool reuse and resizing. Build and tests passed on arm64 against
TouchDesigner 2025.33230's Python 3.11. The included SDK snapshot uses TOP API 12
and Common API 1. Live host loading/docking and real-face footage acceptance
remain pending; older TouchDesigner and macOS versions are not runtime-validated.

## Install

1. Copy `plugin/AppleFaceDetection.plugin` to
   `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/`, or to a
   `Plugins/` folder next to your `.toe` project. Nested folders are supported.
2. Restart TouchDesigner, open the project, and create **Apple Face Detection**
   from the Custom TOP operator list.
3. Connect a TOP containing faces, expand the dock and check the landmark table.
   Use **Create/Repair Landmarks DAT** if it is missing. Check both node errors
   and warnings if setup fails.

The user-wide path follows Derivative's
[Custom Operators installation guide](https://docs.derivative.ca/Custom_Operators).

## Binary package

The version 0.1.0 macOS arm64 archive contains `AppleFaceDetection.plugin`, this
README and `BUILD-INFO.json` with build compatibility and source/binary hashes.
For the binary archive, copy the top-level bundle in step 1 above; the `plugin/`
prefix applies to local source builds. The source-build commands above require
the separate, browsable source distribution folder.

This build uses TouchDesigner 2025.33230's Python 3.11 framework and system Apple
frameworks. It is ad-hoc signed, not Developer ID signed or notarized. Loading in
other TouchDesigner/Python versions is unverified; rebuild from source for a
different Python ABI. Live TouchDesigner and real-face acceptance remain pending.
The archive is a build-validated preview for testing, not a runtime-certified
production release. An external `SHA256SUMS.txt` records the archive checksum.

For acceptance, test single and multiple faces, an empty frame, changing input
resolution, Common resizing/format changes, rename/copy, deleting and repairing
the DAT, and save/reload. Check landmark overlays against the source image.

## Source and attribution

The face detector, geometry and TOP integration were developed for this project.
The local docking bridge and tests adapt the maintained Convex Hull v0.3.0
pattern; host test stubs adapt Segment UV. `TOPOutputHelper.h` is the maintained
workspace helper snapshot copied on 2026-10-17. Relevant helper fixes must be
reviewed and propagated deliberately. All required project files are local;
there are no build dependencies on sibling folders.

The included TouchDesigner SDK headers were copied unchanged from the maintained
Convex Hull snapshot. Their Derivative copyright and license notices are
preserved in the files and apply to those headers. TouchDesigner's Python
framework and headers are supplied by the user's installation and are not
redistributed. No photos, private test projects or model files are distributed.

Apple API references:
[VNDetectFaceLandmarksRequest](https://developer.apple.com/documentation/vision/vndetectfacelandmarksrequest)
and [VNFaceLandmarks2D](https://developer.apple.com/documentation/vision/vnfacelandmarks2d).
