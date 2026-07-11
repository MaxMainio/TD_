# Canny Edge TOP

Derivative-modified TOP sample that converts an input TOP to grayscale and runs OpenCV Canny edge detection. The TouchDesigner node label is `Canny Edge`, and the tested `opType` is `Cannyedge`.

## Input and Output

- Takes one TOP input.
- Downloads the input as `RGBA8Fixed`.
- Converts RGBA to grayscale before edge detection.
- Outputs `Mono8Fixed`.
- Uses the input TOP dimensions.

## Parameters

- `Lowthreshold` / `Low Threshold`: Low Canny threshold in the TouchDesigner UI range 0 to 1. The implementation scales this to 0 to 255 before calling OpenCV.
- `Highthreshold` / `High Threshold`: High Canny threshold in the TouchDesigner UI range 0 to 1. The implementation scales this to 0 to 255 before calling OpenCV.
- `Apperturesize` / `Apperture Size`: Sobel aperture size used by Canny. The spelling is preserved for saved-project compatibility. Runtime clamps this to an odd value between 3 and 7.
- `L2gradient` / `L2 Gradient`: Uses the more accurate L2 gradient magnitude when enabled.

## Dependencies

Requires OpenCV `core` and `imgproc`.

## Notes

- Do not rename `Apperturesize` or its label without accepting saved-project compatibility impact.
- Derivative license headers must be preserved when editing or sharing this code.
