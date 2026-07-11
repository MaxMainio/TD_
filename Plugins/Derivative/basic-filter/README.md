# Basic Filter TOP

Derivative-modified TOP sample that limits the number of color levels in an input image. The TouchDesigner node label is `Filter`, and the tested `opType` is `Basicfilter`.

## Input and Output

- Takes one TOP input.
- Cooks in CPU memory.
- Uses the input TOP dimensions.
- Outputs an RGBA8-style buffer matching the downloaded input format.

## Parameters

- `Bitspercolor` / `Bits per Color`: Number of retained bits per RGB channel, clamped from 1 to 8.
- `Dither`: Enables Floyd-Steinberg-style error diffusion.
- `Multithreaded`: Processes frames on worker threads. This mode intentionally returns completed work from earlier queued frames, so the output can lag the input by several frames.

## Dependencies

This plugin uses the TouchDesigner C++ headers and standard C++ only. It does not require OpenCV.

## Notes

- The visible label `Filter` is generic and may be collision-prone for broader distribution. Do not rename it without accepting saved-project compatibility impact.
- Derivative license headers must be preserved when editing or sharing this code.
