# Cumulative Cost Map

TouchDesigner TOP custom operator that converts one channel of an input TOP into a vertical cumulative cost map.

## TouchDesigner Metadata

- Label: `Cumulative Cost Map`
- Type: `Cumulativecostmap`
- Inputs: 1 TOP
- Execute mode: CPU memory
- Native output: `Mono32Float`

The `Type` value is compatibility-sensitive. Do not rename it casually, because existing `.toe` files may reference it.

## Input

The input TOP is downloaded as `RGBA32Float`. The selected channel is treated as the per-pixel source cost.

## Parameters

| Page | Parameter | Values | Notes |
| --- | --- | --- | --- |
| Cost | Mode | `Minimum`, `Maximum` | Chooses whether each pixel accumulates through the minimum or maximum of the three connected pixels below it. |
| Cost | Channel | `Red`, `Green`, `Blue`, `Alpha` | Selects the input channel used as source cost. |

## Output

The native output is a single-channel `Mono32Float` image. Values are accumulated from the bottom row to the top row, so output values can exceed the normal 0-1 display range. Use a Normalize TOP, Level TOP, or other remapping step when the result needs to be viewed directly.

The Common page `Output Resolution`, `Output Aspect`, and `Pixel Format` settings are honored during final upload. When Common `Pixel Format` is `Use Input`, the cost map is repacked to the connected input TOP's pixel format.

## Dependencies

- TouchDesigner C++ TOP headers
- C++17 compiler
- OpenCV `core`

Current local builds link to Homebrew OpenCV dylibs under `/opt/homebrew/opt/opencv/lib`. That is acceptable for private local development, but distributed binary bundles should either document the Homebrew OpenCV requirement or embed and rewrite the OpenCV dylib install names.

## Build

```sh
cmake -S . -B /tmp/td-cumulative-cost-map-build
cmake --build /tmp/td-cumulative-cost-map-build
```

The CMake project writes the built `.plugin` bundle into the local `plugin/` directory. Build products are intentionally ignored by git.
