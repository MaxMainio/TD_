# Distance Transform TOP

Derivative-modified TOP sample that runs OpenCV distance transform on one selected channel from an input TOP. The TouchDesigner node label is `Distance Transform`, and the tested `opType` is `Distancetransform`.

## Input and Output

- Takes one TOP input.
- Downloads the input as `RGBA8Fixed`.
- Samples one channel from the input, selected by the `Channel` parameter.
- Native output data is `Mono32Float`.
- Honors the Common page `Output Resolution`, `Output Aspect`, and `Pixel Format` settings during final upload.
- When Common `Pixel Format` is `Use Input`, the distance result is repacked to the connected input TOP's pixel format.

## Parameters

- `Distancetype` / `Distance Type`: OpenCV distance metric: `L1`, `L2`, or `C`.
- `Masksize` / `Mask Size`: Transform mask: `Three` (`3x3`), `Five` (`5x5`), or `Precise`.
- `Normalize`: Normalizes the float output to 0 to 1 when enabled.
- `Channel`: Input channel to sample: `R`, `G`, `B`, or `A`.

## Dependencies

Requires OpenCV `core` and `imgproc`.

## Notes

- The input channel is treated as an 8-bit single-channel image before OpenCV distance transform.
- Common page `Fill Viewer` and `Viewer Smoothness` are handled by TouchDesigner at the viewer level.
- Derivative license headers must be preserved when editing or sharing this code.
