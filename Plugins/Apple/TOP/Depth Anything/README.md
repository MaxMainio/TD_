# Apple Depth Anything

Version 1.0.0.

Apple Depth Anything is an Apple Silicon TouchDesigner TOP that estimates relative
depth using bundled Depth Anything V2 Small models. A Custom option also accepts
compatible user-supplied Core ML depth models. No model download or file selection
is required when using the built plugin's two bundled choices.

## Input and Output

Connect one 2D TOP. The plugin downloads BGRA8 pixels, performs inference through
Vision/Core ML, and outputs a `Mono32Float` depth map. Bundled models operate at
518 x 392 internally. These are relative depth values, not calibrated meters.

## Parameters

| Parameter | Behavior |
| --- | --- |
| Model | `Depth Anything V2 Small (F16)` (default), `Depth Anything V2 Small Compact (F16P6)`, or `Custom` |
| Model File | Enabled for Custom only. Accepts a complete `.mlpackage`, `.mlmodel`, or compiled `.mlmodelc`. Its value is retained when switching to a bundled model. |
| Copy Model to Project | Copies the selected original `.mlpackage` into `Packages/AppleDepthAnything/` beside the saved `.toe`, then selects Custom with a project-relative path. |
| Normalize | Remaps each inferred depth frame to 0-1. Default on. |
| Invert | Reverses near/far values after optional normalization. |
| Output Resolution | Match Input crops/resizes the result to the requested output size; Model Native retains model output dimensions. |

Copy Model to Project requires an existing saved `.toe` and a writable project
folder. It copies the currently selected model, not every bundled model. Matching
existing copies are reused; different files at the same destination are never
overwritten. If the project or selection changes during copying, the copy may
finish but the newer selection is preserved. Copying and hashing run off the host
thread; completion is handled on TouchDesigner's independent resource clock, so
timeline playback is not required. Pending actions are cancelled on node deletion.

Custom `.mlmodel` and `.mlmodelc` selections can run, but cannot be exported as an
original `.mlpackage`. Select an original package to use this action. A package
contains both its model specification and weights; do not select the internal
`model.mlmodel` file. Model-copy failures appear in the node warning.

## Naming and Existing Projects

This plugin was previously named **Apple Depth**. Its displayed label and bundle
are now **Apple Depth Anything** / `AppleDepthAnything.plugin`. The registered
operator type is `Appledepthanything`, so newly created nodes are named
`Appledepthanything1`, `Appledepthanything2`, and so on. This changes the old
`Appledepth` type: saved nodes of that type must be recreated using the new
operator, then have their parameters and connections transferred.
On first initialization, a nonempty Model File selects Custom. Later switches
to bundled models retain that file without repeatedly switching back.

Replace the old installed `AppleDepth.plugin` when installing this version;
leaving it installed also leaves its old Apple Depth creation-menu entry. Restart
TouchDesigner after replacing a bundle: a running process retains its loaded
operator metadata. A CPlusPlus TOP that explicitly references the old binary
path needs its Plugin Path updated.
The separate future Depth Pro plugin is not included here.

## Requirements

- Apple Silicon macOS. Both bundled models declare **macOS 14 or newer** in their
  compiled metadata. The native plugin's deployment target remains macOS 12 for
  compatible Custom models; this does not lower the bundled models' requirement.
- TouchDesigner with the included C++ TOP API and matching Python ABI. The current
  development host is **2025.33230 / Python 3.11.15**. Set `TD_APPLICATION` when
  building against a different installation; other host versions need validation.
- Full Xcode (including `coremlcompiler`), CMake 3.20+, and Python 3.9+ for building.
- Vision, Core ML, Core Video, Foundation, Core Foundation, and TouchDesigner's
  Python framework. No PyTorch or Core ML Tools installation is required.

## Build and Install

From this standalone source folder:

```sh
python3 model_assets.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTD_APPLICATION=/Applications/TouchDesigner.app
cmake --build build --parallel 2
```

`model_assets.py` downloads the two Apple-hosted ZIPs, verifies pinned SHA-256
values, and stores complete packages in ignored `models/` directories. Existing
packages are verified without re-downloading. Download URLs and per-file hashes
are in `models/catalog.json`. For offline builds, copy both original packages
into `models/` and run `python3 model_assets.py --verify`. No other repository
folders are required. Downloads fail on a checksum mismatch rather than accepting
silently changed weights.

The finished bundle is `plugin/AppleDepthAnything.plugin`; intermediates are in
`build/`. CMake compiles both models into `.mlmodelc` and embeds those plus the
original `.mlpackage` directories under `Contents/Resources/Models/`. Original
packages total about 69 MB; keeping both forms makes the development bundle about
138 MB (132 MiB). Runtime model lookup uses this plugin's location, not the working
directory or an absolute development path. All resources are copied before
ad-hoc signing the complete bundle.

Copy the complete bundle into TouchDesigner's Plugins directory or a project's
`Plugins` directory, replacing the previous Apple Depth installation, then restart
TouchDesigner. The supplied local release bundle is ad-hoc signed. Developer ID
signing/notarization is a separate step; this build is not notarized.

Advanced build settings:

- `OUTPUT_DIRECTORY`: redirects the resulting bundle.
- `MODEL_DIRECTORY`: locates original packages elsewhere (useful when keeping
  generated assets out of a source-only release snapshot).
- `python3 model_assets.py --directory /path/to/models`: downloads into that folder.
- `COREML_COMPILER`: selects the compiler if Xcode is in a nonstandard location.
- `TD_ADHOC_SIGN=OFF`: disables local ad-hoc signing for a separately managed workflow.

## Model Sources and Licenses

[Apple's gallery](https://developer.apple.com/machine-learning/models/) supplies
`DepthAnythingV2SmallF16.mlpackage` and `DepthAnythingV2SmallF16P6.mlpackage`.
The [Apple model card](https://huggingface.co/apple/coreml-depth-anything-v2-small)
identifies Apache-2.0 for the Small models. Attribution and the original license
are preserved in `licenses/` and copied into the bundle. The upstream project is
[Depth Anything V2](https://github.com/DepthAnything/Depth-Anything-V2), described
in [the V2 paper](https://arxiv.org/abs/2406.09414).

These model terms do not extend to every Depth Anything variant or to Depth Pro.
TouchDesigner SDK headers retain Derivative's license notices. Original model
weights are unchanged; `.mlmodelc` resources are generated from those packages.

## Validation and Limitations

```sh
cmake -S . -B build -DTD_BUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Tests cover all 65,536 half-float encodings, project-copy conflicts/cancellation,
legacy selection migration, bundle-relative resolution, and real Vision inference
for both models on synthetic imagery. The model test requires access to macOS
Core ML services; a restrictive process sandbox can prevent inference. The
optional `tests/touchdesigner_validate.py` exercises the registered Custom TOP in
an explicitly supplied saved test project, including paused copy completion.

The 1.0.0 build passes all three automated suites. Live testing
confirmed registration, menu choices, the disabled Model File state and F16
inference. Full live copy/reload acceptance remains pending after a host API fix;
the validation host subsequently opened without an accessible window. This is a
validation limitation, not a claim that every supported host has been tested.

Large inputs still require a CPU download and synchronous inference per cook.
Custom source models compile on selection and can cause a loading pause. Output
orientation/cropping follows Vision's scale-fit behavior. Performance on video,
real-scene depth quality, older supported macOS versions, and other TouchDesigner
builds require separate acceptance checks.
