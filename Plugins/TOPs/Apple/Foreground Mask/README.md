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

Requirements: CMake 3.20 or newer and Apple's command-line developer tools
with a macOS SDK containing the frameworks/APIs listed above. Use a macOS 26 SDK
or newer for Apple Super Resolution. These projects target Apple Silicon only;
Windows and Intel builds are not supported by this configuration. The maintained
TouchDesigner test target is 2025.33230; compatibility with other versions and
older macOS installations requires runtime verification.

From this downloaded or cloned plugin folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The finished bundle is `plugin/AppleForegroundMask.plugin`; intermediates stay in `build/`.
An optional `OUTPUT_DIRECTORY` CMake setting redirects the bundle for validation.
Copy the complete bundle into `~/Library/Application Support/Derivative/TouchDesigner099/Plugins`,
or into a project-specific `Plugins` folder next to a `.toe`, then restart
TouchDesigner. The included SDK headers retain their Derivative copyright and
license notices. No parent or sibling source folders are needed to build.

## Limitations

- Foreground quality depends on Apple's Vision model and the source image content.
- High-resolution inputs require a CPU texture download before Vision processing.
- Release bundles currently use ad-hoc linker signatures; sign and notarize deliberately before distribution.
