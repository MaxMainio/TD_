# Apple Subject Mask

Apple Subject Mask is a TouchDesigner TOP that uses Vision person segmentation to create a subject matte.

## Input

- One 2D TOP input.
- The input is downloaded as BGRA8Fixed CPU memory for Vision.

## Parameters

- `Quality`: Vision segmentation quality: `Accurate`, `Balanced`, or `Fast`.
- `Resolution`: `Match Input` resizes the generated mask back to the incoming TOP size; `Mask Native` keeps Vision's mask size.

## Output

- `Mono8Fixed` TOP where brighter pixels represent the segmented person/subject.

## Dependencies

- TouchDesigner C++ TOP API headers included in this folder.
- Apple Vision, Core Video, Foundation, and Core Foundation frameworks.
- Apple Silicon (`arm64`) build.

## Minimum macOS

- CMake sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `12.0`.
- The implementation uses `VNGeneratePersonSegmentationRequest`.

## Build and Install

From this folder:

```sh
cmake -S . -B build
cmake --build build
```

Copy the generated `AppleSubjectMask.plugin` bundle to a TouchDesigner Plugins folder, or to a project-specific `Plugins` folder next to a `.toe`.

## Limitations

- The request targets people, not arbitrary objects.
- Mask edges and temporal stability depend on Apple's Vision model.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
