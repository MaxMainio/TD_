# Apple Saliency Map

Apple Saliency Map is a TouchDesigner TOP that uses Vision saliency requests to produce an attention or objectness map.

## Input

- One 2D TOP input.
- The input is downloaded as BGRA8Fixed CPU memory for Vision.

## Parameters

- `Mode`: `Attention` or `Objectness`.
- `Resolution`: `Match Input` resizes the generated saliency map back to the incoming TOP size; `Saliency Native` keeps Vision's saliency output size.

## Output

- `Mono32Float` TOP where brighter pixels represent stronger saliency.

## Dependencies

- TouchDesigner C++ TOP API headers included in this folder.
- Apple Vision, Core Video, Foundation, and Core Foundation frameworks.
- Apple Silicon (`arm64`) build.

## Minimum macOS

- CMake sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `11.0`.
- The implementation uses `VNGenerateAttentionBasedSaliencyImageRequest` and `VNGenerateObjectnessBasedSaliencyImageRequest`.

## Build and Install

From this folder:

```sh
cmake -S . -B build
cmake --build build
```

Copy the generated `AppleSaliencyMap.plugin` bundle to a TouchDesigner Plugins folder, or to a project-specific `Plugins` folder next to a `.toe`.

## Limitations

- The internal `opType` is currently `Applesaliencymap`; keep this for saved `.toe` compatibility unless a migration is planned.
- Saliency results are model-dependent and may be coarse compared with the source resolution.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
