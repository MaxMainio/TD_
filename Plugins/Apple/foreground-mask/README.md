# Apple Foreground Mask

Apple Foreground Mask is a TouchDesigner TOP that uses Vision foreground instance masking and returns a combined foreground matte.

## Input

- One 2D TOP input.
- The input is downloaded as BGRA8Fixed CPU memory for Vision.

## Parameters

- `Resolution`: `Match Input` resizes the generated mask back to the incoming TOP size; `Mask Native` keeps Vision's mask size.

## Output

- `Mono32Float` TOP where brighter pixels represent foreground confidence.

## Dependencies

- TouchDesigner C++ TOP API headers included in this folder.
- Apple Vision, Core Video, Foundation, and Core Foundation frameworks.
- Apple Silicon (`arm64`) build.

## Minimum macOS

- CMake sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `14.0`.
- The implementation uses `VNGenerateForegroundInstanceMaskRequest`.

## Build and Install

From this folder:

```sh
cmake -S . -B build
cmake --build build
```

Copy the generated `AppleForegroundMask.plugin` bundle to a TouchDesigner Plugins folder, or to a project-specific `Plugins` folder next to a `.toe`.

## Limitations

- Foreground quality depends on Apple's Vision model and the source image content.
- High-resolution inputs require a CPU texture download before Vision processing.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
