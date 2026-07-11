# Seam Carving

TouchDesigner TOP custom operator that resizes an image horizontally by removing or inserting low-cost vertical seams.

## TouchDesigner Metadata

- Label: `Seam Carving`
- Type: `Seamcarving`
- Inputs: 1-2 TOPs
- Execute mode: CPU memory
- Output: `RGBA32Float`

The `Type` value is compatibility-sensitive. Do not rename it casually, because existing `.toe` files may reference it.

## Inputs

Input 0 is the source image. It is downloaded as `RGBA32Float`.

Input 1 is optional external seam energy. It is downloaded as `Mono32Float`. If its dimensions do not match the source image, it is resized to the source dimensions with nearest-neighbor sampling. When input 1 is not connected, the operator computes internal energy from source-image luminance gradients.

Lower energy values are preferred by the seam tracing path.

## Parameters

| Page | Parameter | Values | Notes |
| --- | --- | --- | --- |
| Carving | Carve Width | -256 to 256 | Positive values remove vertical seams and reduce output width. Negative values insert seams and increase output width. Zero passes the image through. |
| Carving | Cost Update | `Once`, `Targeted`, `Global` | Controls how often cumulative cost is recomputed while multiple seams are removed or inserted. |

## Cost Update Modes

- `Once`: compute the cumulative cost map once, then shrink it as seams are processed.
- `Targeted`: update a local band around each changed seam path.
- `Global`: recompute energy and cumulative cost for each seam.

`Targeted` is the default. It is usually a practical balance between speed and quality.

## Output

The output is `RGBA32Float`. Height matches the source image. Width is adjusted by `Carve Width`, subject to safety clamps in the implementation.

Seam insertion creates new pixels by averaging neighboring source pixels around the selected seam path.

## Dependencies

- TouchDesigner C++ TOP headers
- C++17 compiler
- OpenCV `core`

Current local builds link to Homebrew OpenCV dylibs under `/opt/homebrew/opt/opencv/lib`. That is acceptable for private local development, but distributed binary bundles should either document the Homebrew OpenCV requirement or embed and rewrite the OpenCV dylib install names.

## Build

```sh
cmake -S . -B /tmp/td-seam-carving-build
cmake --build /tmp/td-seam-carving-build
```

The CMake project writes the built `.plugin` bundle into the local `plugin/` directory. Build products are intentionally ignored by git.

