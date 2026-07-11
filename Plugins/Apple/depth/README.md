# Apple Depth

Apple Depth is a TouchDesigner TOP that runs a user-selected Core ML depth-estimation model through Vision and outputs a depth map.

## Input

- One 2D TOP input.
- The input is downloaded as BGRA8Fixed CPU memory before inference.

## Parameters

- `Normalize`: remaps the model output to a 0-1 range.
- `Invert`: flips near/far values after optional normalization.
- `Resolution`: `Match Input` resizes/crops the model result back to the incoming TOP size; `Model Native` keeps the model output size.
- `Model File`: path to a local `.mlmodel`, `.mlpackage`, or compiled Core ML model bundle.

## Output

- `Mono32Float` TOP containing one floating-point depth value per pixel.

## Dependencies

- TouchDesigner C++ TOP API headers included in this folder.
- Apple Vision, Core ML, Core Video, Foundation, and Core Foundation frameworks.
- Apple Silicon (`arm64`) build.

## Minimum macOS

- CMake sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `12.0`.
- Actual runtime compatibility can still depend on the selected Core ML model and the host Mac's Vision/Core ML support.

## Model Notes

The repository does not include depth models and does not assume a fixed model path. Select the model manually with the `Model File` parameter.

## Build and Install

From this folder:

```sh
cmake -S . -B build
cmake --build build
```

Copy the generated `AppleDepth.plugin` bundle to a TouchDesigner Plugins folder, or to a project-specific `Plugins` folder next to a `.toe`.

## Limitations

- Large inputs require a CPU download and Core ML inference every cook.
- Output orientation and scaling are tuned for Vision's scale-fit behavior.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
