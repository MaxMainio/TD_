"""Manual host checks. Execute this file in TouchDesigner's Python Textport,
then call run_validation('/absolute/path/ErrorDiffusionDither.plugin').
Creates a uniquely named test Base COMP; does not save or replace a project.
"""
from pathlib import Path
import json
import tempfile
import time

import numpy as np


MATRICES = [
    (16, [[0, 0, 0, 7, 0], [0, 3, 5, 1, 0]]),
    (48, [[0, 0, 0, 7, 5], [3, 5, 7, 5, 3], [1, 3, 5, 3, 1]]),
    (42, [[0, 0, 0, 8, 4], [2, 4, 8, 4, 2], [1, 2, 4, 2, 1]]),
    (8, [[0, 0, 0, 1, 1], [0, 1, 1, 1, 0], [0, 0, 1, 0, 0]]),
    (32, [[0, 0, 0, 8, 4], [2, 4, 8, 4, 2]]),
    (32, [[0, 0, 0, 5, 3], [2, 4, 5, 4, 2], [0, 2, 3, 2, 0]]),
    (16, [[0, 0, 0, 4, 3], [1, 2, 3, 2, 1]]),
    (4, [[0, 0, 0, 2, 0], [0, 1, 1, 0, 0]]),
]


def _reference(image, kernel, bits, reverse=False, strength=1, mono=None, alpha="Preserve"):
    result = image.copy()
    if mono is not None:
        value = (.2126 * image[:, :, 0] + .7152 * image[:, :, 1] + .0722 * image[:, :, 2]) if mono == 0 else image[:, :, mono - 1]
        result[:, :, :3] = value[:, :, None]
    levels = np.float32((1 << bits) - 1)
    height, width, _ = result.shape
    divisor, matrix = MATRICES[kernel]
    for channel in range(4 if alpha == "Dither" else 3):
        work = result[:, :, channel].copy() * levels
        for y in range(height):
            direction = -1 if reverse and y % 2 else 1
            for x in (range(width - 1, -1, -1) if direction < 0 else range(width)):
                old = work[y, x]
                quantized = np.round(old)
                work[y, x] = quantized
                error = (old - quantized) * np.float32(strength)
                for dy, row in enumerate(matrix):
                    for column, numerator in enumerate(row):
                        nx, ny = x + direction * (column - 2), y + dy
                        if numerator and 0 <= nx < width and ny < height:
                            work[ny, nx] += error * np.float32(numerator / divisor)
        result[:, :, channel] = np.clip(work / levels, 0, 1)
    if alpha == "Opaque":
        result[:, :, 3] = 1
    return result


def run_validation(plugin_path, parent_path="/project1"):
    bundle = Path(plugin_path).expanduser().resolve()
    if not bundle.is_dir():
        raise ValueError(f"Plugin bundle not found: {bundle}")
    parent_op = op(parent_path)
    if parent_op is None:
        raise ValueError(f"Parent COMP not found: {parent_path}")
    group = parent_op.create(baseCOMP, f"error_diffusion_validation_{time.time_ns()}")
    rng = np.random.default_rng(7241)
    fixture = rng.random((9, 13, 4), dtype=np.float32)
    group.store("fixture", fixture)
    callbacks = group.create(textDAT, "source_callbacks")
    callbacks.text = "def onCook(scriptOp):\n    scriptOp.copyNumpyArray(scriptOp.parent().fetch('fixture'))\n"
    source = group.create(scriptTOP, "source")
    source.par.callbacks = callbacks
    source.par.format = "rgba32float"
    source.cook(force=True)
    native = group.create(cplusplusTOP, "dither")
    native.inputConnectors[0].connect(source)
    native.par.plugin = str(bundle)
    native.cook(force=True)
    if not hasattr(native.par, "Algorithm"):
        raise RuntimeError(f"Plugin has not loaded; review its load message in {group.path}")
    native.par.format = "rgba32float"
    tested = 0

    def read_result():
        native.cook(force=True)
        if native.errors():
            raise RuntimeError(native.errors())
        return native.numpyArray(delayed=False).copy()

    for kernel in range(8):
        native.par.Algorithm.menuIndex = min(kernel, 5)
        native.par.Alternate.menuIndex = max(0, kernel - 5)
        for bits in range(1, 9):
            native.par.Bitdepth = bits
            for reverse in (False, True):
                native.par.Scanpattern = "Serpentine" if reverse else "Raster"
                actual = read_result()
                np.testing.assert_array_equal(actual, _reference(fixture, kernel, bits, reverse))
                tested += 1
        assert native.par.Alternate.enable == (kernel >= 5)

    native.par.Algorithm.menuIndex = 0
    native.par.Scanpattern = "Raster"
    native.par.Bitdepth = 3
    for mono in range(5):
        native.par.Colormode = "Monochrome"
        native.par.Monosource.menuIndex = mono
        for alpha in ("Preserve", "Dither", "Opaque"):
            native.par.Alpha = alpha
            for strength in (0, .37, 1):
                native.par.Diffusion = strength
                actual = read_result()
                np.testing.assert_array_equal(actual, _reference(fixture, 0, 3, strength=strength, mono=mono, alpha=alpha))
                tested += 1
    assert native.par.Monosource.enable
    native.par.Colormode = "RGB"
    native.par.Alpha = "Preserve"
    native.par.Diffusion = 1
    native.par.outputresolution = "custom"
    native.par.resolutionw, native.par.resolutionh = 5, 3
    actual = read_result()
    xs = np.floor((np.arange(5) + .5) * 13 / 5).astype(int)
    ys = np.floor((np.arange(3) + .5) * 9 / 3).astype(int)
    np.testing.assert_array_equal(actual, _reference(fixture[ys[:, None], xs], 0, 3))
    native.par.outputaspect = "custom"
    native.par.aspect1, native.par.aspect2 = 2, 1
    read_result()
    assert abs(native.aspect - 2) < 1e-6
    native.par.outputaspect = "useinput"
    native.par.outputresolution = "useinput"

    formats = ["rgba8fixed", "rgba16fixed", "rgba16float", "rgba32float", "rgb10a2fixed", "rgba11float"]
    formats += [prefix + depth for prefix in ("mono", "rg", "a", "monoalpha")
                for depth in ("8fixed", "16fixed", "16float", "32float")]
    formats_checked = []
    for format_name in formats:
        if format_name not in native.par.format.menuNames:
            continue
        native.par.format = format_name
        actual = read_result()
        assert np.isfinite(actual).all()
        assert actual.shape[:2] == fixture.shape[:2]
        formats_checked.append(format_name)
    native.par.format = "useinput"
    read_result()
    assert native.pixelFormatName == source.pixelFormatName
    native.inputConnectors[0].disconnect()
    native.cook(force=True)
    assert native.errors()
    native.inputConnectors[0].connect(source)
    np.testing.assert_array_equal(read_result(), _reference(fixture, 0, 3))
    for height, width in [(1, 1), (1, 19), (17, 1), (65, 67)]:
        group.store("fixture", rng.random((height, width, 4), dtype=np.float32))
        source.cook(force=True)
        for _ in range(5):
            temporary = group.create(cplusplusTOP, "temporary")
            temporary.par.plugin = str(bundle)
            temporary.inputConnectors[0].connect(source)
            temporary.cook(force=True)
            assert not temporary.errors(), temporary.errors()
            temporary.destroy()
        assert read_result().shape[:2] == (height, width)

    group.store("fixture", fixture)
    source.cook(force=True)
    read_result()
    info = group.create(infoCHOP, "diagnostics")
    info.par.op = native.path
    info.cook(force=True)
    report = {"status": "passed", "build": str(app.version), "network": group.path,
              "pixel_comparisons": tested + 2, "formats": formats_checked,
              "diagnostics": {c.name: float(c[0]) for c in info.chans()}}
    result_file = Path(tempfile.gettempdir()) / f"error-diffusion-host-{time.time_ns()}.json"
    result_file.write_text(json.dumps(report, indent=2))
    group.layoutChildren()
    print(f"Host checks passed. Results: {result_file}\nReview network: {group.path}")
    return report
