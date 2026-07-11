# Apple Super Resolution

Apple Super Resolution is a TouchDesigner TOP that uses VideoToolbox Super Resolution to upscale image or video input.

## Input

- One 2D TOP input.
- The input is downloaded as BGRA8Fixed CPU memory and copied into a Core Video pixel buffer.

## Parameters

- `Scale Factor`: requested `2x`, `3x`, or `4x` upscale.
- `Input Type`: `Image` processes each cook independently; `Video` keeps a VideoToolbox session and previous-frame history.
- `Reset Video`: clears the Video mode session and history.
- `Download Model`: requests the VideoToolbox model for the most recently cooked scale factor and input type. If the selected scale is unsupported, the plugin reports supported scale factors and falls back predictably.

## Output

- `BGRA8Fixed` TOP at the selected or fallback output size.

## Dependencies

- TouchDesigner C++ TOP API headers included in this folder.
- Apple VideoToolbox, Core Video, Core Media, Foundation, and Core Foundation frameworks.
- Apple Silicon (`arm64`) build.

## Minimum macOS

- CMake sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `26.0`.
- The implementation uses VideoToolbox Super Resolution APIs such as `VTSuperResolutionScalerConfiguration`.

## Build and Install

From this folder:

```sh
cmake -S . -B build
cmake --build build
```

Copy the generated `AppleSuperResolution.plugin` bundle to a TouchDesigner Plugins folder, or to a project-specific `Plugins` folder next to a `.toe`.

## Runtime Testing

In TouchDesigner, feed a small still image or movie into the TOP, try `2x`, `3x`, and `4x`, and watch the textport for supported-scale or model-download messages. Use `Reset Video` after switching between still/video sources or after discontinuous timeline jumps.

## Limitations

- Supported scale factors are reported by VideoToolbox at runtime and may vary by system.
- Inputs larger than 1920 x 1920 are rejected by the plugin before configuration.
- Video mode depends on sequential frames; still images usually work better in Image mode.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
