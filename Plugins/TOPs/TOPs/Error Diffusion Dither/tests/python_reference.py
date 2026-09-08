"""Compare the engine to the eight reviewed onCook scripts in the original ZIP.

The archive is an explicit local test input, not a plugin runtime dependency.
Only the eight named algorithm DATs are loaded. The scripts' callback bodies
are preserved; guard pixels adapt their fixed loop bounds to the corrected
image boundaries. No synthetic rows are processed before the image.
"""
import argparse
import csv
from pathlib import Path
import resource
import subprocess
import time
from types import SimpleNamespace
import zipfile

import numpy as np

NAMES = ["Floyd-Steinberg", "Jarvis-Judice-Ninke", "Stucki", "Atkinson",
         "Burkes", "Sierra Full", "Sierra Two-Row", "Sierra Lite"]
PADDING = [(1, 1), (2, 2), (2, 2), (2, 2), (2, 1), (2, 2), (2, 1), (2, 1)]
PREFIX = ("Error_Diffusion_Dither.tox/Error_Diffusion_Dither.tox.dir/"
          "Error_Diffusion_Dither/scripts/")


def load_scripts(archive):
    callbacks = []
    with zipfile.ZipFile(archive) as source:
        for index in range(1, 9):
            data = source.read(PREFIX + f"text{index}.text")
            code = data[data.index(b"#"):].decode("utf-8")
            namespace = {}
            exec(compile(code, f"original/text{index}", "exec"), namespace)
            callbacks.append(namespace["onCook"])
    return callbacks


def original_channel(callback, channel, bits, kernel):
    horizontal, vertical = PADDING[kernel]
    image = np.pad(channel, ((0, vertical), (horizontal, horizontal)))[:, :, None]
    result = []
    operator = SimpleNamespace(
        par=SimpleNamespace(Depth=SimpleNamespace(val=bits)),
        inputs=[SimpleNamespace(numpyArray=lambda delayed=False: image)],
        copyNumpyArray=result.append,
    )
    callback(operator)
    return result[0][:channel.shape[0], horizontal:horizontal + channel.shape[1], 0]


def validate(callbacks, driver, image_path=None):
    from touchdesigner_validate import _reference
    count = 0
    rng = np.random.default_rng(18375)
    for width, height in [(1, 1), (1, 9), (13, 1), (2, 2), (17, 11)]:
        image = rng.uniform(-.15, 1.15, (height, width, 4)).astype(np.float32)
        image[0, 0, 0] = .5
        for kernel in range(8):
            for bits in range(1, 9):
                expected = (original_channel(callbacks[kernel], image[:, :, 0], bits, kernel)
                            if callbacks else _reference(image, kernel, bits)[:, :, 0])
                result = subprocess.run([str(driver), str(kernel), str(bits), str(width), str(height)],
                                        input=image.tobytes(), capture_output=True, check=True)
                actual = np.frombuffer(result.stdout, dtype=np.float32).reshape(height, width)
                np.testing.assert_array_equal(actual, expected,
                    err_msg=f"{NAMES[kernel]}, bits={bits}, size={width}x{height}")
                count += 1
    y, x = np.mgrid[:33, :64].astype(np.float32)
    gradient = np.stack([x / 63, y / 32, (x + y) / 95, np.ones_like(x)], axis=2)
    transparent = gradient.copy()
    transparent[:, :, 3] = x / 63
    transparent[:, :, :3] *= transparent[:, :, 3:4]
    extra_images = [("gradient", gradient), ("transparent", transparent)]
    if image_path:
        from PIL import Image
        photo = np.asarray(Image.open(image_path).convert("RGBA").resize((64, 64)), dtype=np.float32) / 255
        extra_images.append(("photograph", np.flipud(photo).copy()))
    for label, image in extra_images:
        height, width = image.shape[:2]
        for kernel in range(8):
            for bits in (1, 3):
                for channel in range(3):
                    selected = image.copy()
                    selected[:, :, 0] = image[:, :, channel]
                    expected = (original_channel(callbacks[kernel], selected[:, :, 0], bits, kernel)
                                if callbacks else _reference(selected, kernel, bits)[:, :, 0])
                    result = subprocess.run([str(driver), str(kernel), str(bits), str(width), str(height)],
                                            input=selected.tobytes(), capture_output=True, check=True)
                    actual = np.frombuffer(result.stdout, dtype=np.float32).reshape(height, width)
                    np.testing.assert_array_equal(actual, expected,
                        err_msg=f"{label}, {NAMES[kernel]}, bits={bits}, channel={channel}")
                    count += 1
    reference_name = "original NumPy onCook scripts" if callbacks else "independent NumPy matrix reference"
    print(f"PASS: {count} exact comparisons against {reference_name} (NumPy {np.__version__}).")


def benchmark(callbacks, path):
    # Single measured frame per case: original Python is intentionally slow.
    # This measures CPU kernels/copies, not TouchDesigner or GPU transfer time.
    rows = []
    for width, height in [(512, 512), (1920, 1080)]:
        x = np.arange(width, dtype=np.float32) / (width - 1)
        y = np.arange(height, dtype=np.float32) / (height - 1)
        image = np.empty((height, width, 4), dtype=np.float32)
        image[:, :, 0] = x
        image[:, :, 1] = y[:, None]
        rng = 713135
        blue = np.empty(width * height, dtype=np.float32)
        for i in range(blue.size):
            rng = (rng * 1664525 + 1013904223) & 0xffffffff
            blue[i] = np.float32(rng >> 8) / np.float32(16777215)
        image[:, :, 2] = blue.reshape(height, width)
        image[:, :, 3] = .7
        for kernel, callback in enumerate(callbacks):
            original_channel(callback, image[:8, :8, 0], 1, kernel)
            for mono in [False, True]:
                start_cpu, start = time.process_time(), time.perf_counter()
                if mono:
                    source = (.2126 * image[:, :, 0] + .7152 * image[:, :, 1] + .0722 * image[:, :, 2]).astype(np.float32)
                    processed = [original_channel(callback, source, 1, kernel)] * 3
                else:
                    processed = [original_channel(callback, image[:, :, c], 1, kernel) for c in range(3)]
                result = np.stack(processed + [image[:, :, 3]], axis=2)
                packed = np.floor(np.clip(result, 0, 1) * 255 + .5).astype(np.uint8)
                assert packed.shape == image.shape
                elapsed = (time.perf_counter() - start) * 1000
                rows.append([width, height, NAMES[kernel], "Monochrome" if mono else "RGB", 1,
                             elapsed, (time.process_time() - start_cpu) * 1000,
                             resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (1024 * 1024)])
                with Path(path).open("w", newline="") as output:
                    writer = csv.writer(output)
                    writer.writerow(["width", "height", "kernel", "color", "samples", "total_ms",
                                     "cpu_ms_per_frame", "peak_rss_mib"])
                    writer.writerows(rows)
                print(f"{width}x{height} {NAMES[kernel]} {rows[-1][3]}: {elapsed:.1f} ms", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, help="Optional original expanded COMP ZIP")
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--benchmark", type=Path)
    parser.add_argument("--image", type=Path, help="Optional photograph fixture (requires Pillow)")
    args = parser.parse_args()
    callbacks = load_scripts(args.archive) if args.archive else None
    validate(callbacks, args.driver, args.image)
    if args.benchmark:
        if callbacks is None:
            parser.error("--benchmark requires --archive to time the original callbacks")
        benchmark(callbacks, args.benchmark)
