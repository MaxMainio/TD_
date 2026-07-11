# Apple Optical Flow

Apple Optical Flow is a TouchDesigner TOP that uses Vision optical flow to estimate motion between the previous and current input frames.

## Input

- One 2D TOP input.
- The input is downloaded as BGRA8Fixed CPU memory for Vision.

## Parameters

- `Accuracy`: Vision optical-flow accuracy level: `Low`, `Medium`, or `High`.
- `Normalize`: converts signed motion vectors into a display-friendly 0-1 range.
- `Normalize Scale`: scale used when `Normalize` is enabled.
- `Reset History`: clears the stored previous frame.

## Output

- `RG32Float` TOP. Red and green contain X/Y flow, optionally normalized into a display-friendly range.

## Dependencies

- TouchDesigner C++ TOP API headers included in this folder.
- Apple Vision, Core Video, Foundation, and Core Foundation frameworks.
- Apple Silicon (`arm64`) build.

## Minimum macOS

- CMake sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `11.0`.
- The implementation uses `VNGenerateOpticalFlowRequest`.

## Build and Install

From this folder:

```sh
cmake -S . -B build
cmake --build build
```

Copy the generated `AppleOpticalFlow.plugin` bundle to a TouchDesigner Plugins folder, or to a project-specific `Plugins` folder next to a `.toe`.

## Limitations

- The first cook after creation or reset has no previous frame and will not output flow.
- Motion quality depends on frame continuity and Vision's optical-flow model.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
