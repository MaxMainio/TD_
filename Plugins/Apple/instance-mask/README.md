# Apple Instance Mask

Apple Instance Mask is a TouchDesigner TOP that uses Vision person instance masking and outputs a matte for all people or one selected person instance.

## Input

- One 2D TOP input.
- The input is downloaded as BGRA8Fixed CPU memory for Vision.

## Parameters

- `Instance`: selects all detected people or an indexed person instance.
- `Resolution`: `Match Input` resizes the generated mask back to the incoming TOP size; `Mask Native` keeps Vision's mask size.

## Output

- `Mono32Float` TOP where brighter pixels represent the selected person mask.

## Dependencies

- TouchDesigner C++ TOP API headers included in this folder.
- Apple Vision, Core Video, Foundation, and Core Foundation frameworks.
- Apple Silicon (`arm64`) build.

## Minimum macOS

- CMake sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `14.0`.
- The implementation uses `VNGeneratePersonInstanceMaskRequest`.

## Build and Install

From this folder:

```sh
cmake -S . -B build
cmake --build build
```

Copy the generated `AppleInstanceMask.plugin` bundle to a TouchDesigner Plugins folder, or to a project-specific `Plugins` folder next to a `.toe`.

## Limitations

- Only person instances are targeted.
- Instance indexes depend on Vision's detection order and may change frame to frame.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
