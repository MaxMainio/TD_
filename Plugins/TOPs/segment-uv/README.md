# Segment UV

TouchDesigner TOP custom operator that finds connected alpha-mask segments and fills each segment with a representative coordinate or repeatable random color.

## TouchDesigner Metadata

- Label: `Segment UV`
- Type: `Segmentuv`
- Inputs: 1 TOP
- Execute mode: CPU memory
- Output: `RGBA32Float`

The `Type` value is compatibility-sensitive. Do not rename it casually, because existing `.toe` files may reference it.

## Input

The input TOP is downloaded as `RGBA32Float`. The alpha channel is thresholded to create a binary mask, and connected foreground pixels become segments.

## Parameters

| Page | Parameter | Values | Notes |
| --- | --- | --- | --- |
| Segmentation | Method | `Centroid`, `Boundingboxcenter`, `Medianpixelcoordinate`, `Closestsegmentpixeltocentroid`, `Random` | Selects the value assigned to each segment. All methods except `Random` output UV-like pixel coordinates in red and green. |
| Segmentation | Seed | 1-9999 | Seeds the repeatable random color output used by `Random`. |
| Segmentation | Alpha Threshold | 0-1 | Pixels with alpha greater than this value are included in the segment mask. |

## Output

The output is `RGBA32Float`.

For coordinate methods, every pixel in a segment receives the same representative coordinate:

- Red: normalized X coordinate.
- Green: normalized Y coordinate.
- Blue: 0.
- Alpha: 1.

For `Random`, every segment receives a repeatable random RGB color with alpha 1. Background pixels are transparent black.

## Performance Notes

The operator uses connected-component analysis on the alpha mask. `Medianpixelcoordinate` and `Closestsegmentpixeltocentroid` scan pixels inside each segment, so they can become expensive on high-resolution masks or masks with many large components.

## Dependencies

- TouchDesigner C++ TOP headers
- C++17 compiler
- OpenCV `core`
- OpenCV `imgproc`

Current local builds link to Homebrew OpenCV dylibs under `/opt/homebrew/opt/opencv/lib`. That is acceptable for private local development, but distributed binary bundles should either document the Homebrew OpenCV requirement or embed and rewrite the OpenCV dylib install names.

## Build

```sh
cmake -S . -B /tmp/td-segment-uv-build
cmake --build /tmp/td-segment-uv-build
```

The CMake project writes the built `.plugin` bundle into the local `plugin/` directory. Build products are intentionally ignored by git.

