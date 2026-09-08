"""Run in TouchDesigner's Python Textport; see README for the two-line invocation.

Creates only a uniquely named test Base COMP. Does not save the project.
Uses NumPy bundled with TouchDesigner; no OpenCV is used by this test or plugin.
"""
from pathlib import Path
import json
import math
import tempfile
import time

import numpy as np


def _turn(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _hull(points):
    if not points:
        return []
    current = start = min(points)
    result = []
    while True:
        result.append(current)
        nxt = current
        for candidate in points:
            turn = _turn(current, nxt, candidate)
            distance = lambda p: (p[0] - current[0]) ** 2 + (p[1] - current[1]) ** 2
            if nxt == current or turn < 0 or (turn == 0 and distance(candidate) > distance(nxt)):
                nxt = candidate
        current = nxt
        if current == start:
            return result


def _blobs(mask):
    height, width = mask.shape
    visited = np.zeros_like(mask)
    blobs = []
    for y in range(height):
        for x in range(width):
            if not mask[y, x] or visited[y, x]:
                continue
            points = [(x, y)]
            visited[y, x] = True
            for px, py in points:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = px + dx, py + dy
                        if 0 <= nx < width and 0 <= ny < height and mask[ny, nx] and not visited[ny, nx]:
                            visited[ny, nx] = True
                            points.append((nx, ny))
            blobs.append(points)
    return blobs


def _raster(vertices, shape, view, thickness):
    result = np.zeros(shape, np.float32)
    if not vertices:
        return result
    edges = [(vertices[i], vertices[(i + 1) % len(vertices)])
             for i in range(1 if len(vertices) <= 2 else len(vertices))]
    for y in range(shape[0]):
        for x in range(shape[1]):
            inside = view == "Filled" and len(vertices) >= 3 and all(_turn(a, b, (x, y)) >= 0 for a, b in edges)
            boundary = False
            for a, b in edges:
                width = thickness if view == "Outline" else 1
                if width == 1:
                    major = 0 if abs(b[0] - a[0]) >= abs(b[1] - a[1]) else 1
                    minor = 1 - major
                    p = (x, y)
                    if a == b:
                        boundary |= p == a
                    elif min(a[major], b[major]) <= p[major] <= max(a[major], b[major]):
                        fraction = (p[major] - a[major]) / (b[major] - a[major])
                        boundary |= p[minor] == math.floor(a[minor] + fraction * (b[minor] - a[minor]) + .5 + 1e-12)
                else:
                    shift = .5 if width % 2 == 0 else 0
                    dx, dy = b[0] - a[0], b[1] - a[1]
                    t = min(1, max(0, ((x - a[0] - shift) * dx + (y - a[1] - shift) * dy) / (dx * dx + dy * dy))) if dx or dy else 0
                    boundary |= math.hypot(x - a[0] - shift - t * dx, y - a[1] - shift - t * dy) <= width / 2 + 1e-12
            result[y, x] = inside or boundary
    return result


def _reference(image, source="Luminance", grouping="Global", view="Filled", threshold=.5,
               independent=False, thresholds=(0, .25, .75, 1), below=False, minimum=1,
               ignore=False, cutoff=0, alpha="Opaque", thickness=1):
    sources = {"Red": 0, "Green": 1, "Blue": 2, "Alpha": 3}
    channels = list(range(4 if source == "RGBA" else 3)) if source in ("RGB", "RGBA") else [sources.get(source, -1)]
    planes = []
    for channel in channels:
        values = ((np.float32(.2126) * image[:, :, 0] + np.float32(.7152) * image[:, :, 1])
                  + np.float32(.0722) * image[:, :, 2]) if channel == -1 else image[:, :, channel]
        t = thresholds[channel] if independent and len(channels) > 1 else threshold
        mask = np.isfinite(values) & ((values <= t) if below else (values > t))
        if ignore:
            mask &= np.isfinite(image[:, :, 3]) & (image[:, :, 3] > cutoff)
        blobs = [b for b in _blobs(mask) if len(b) >= minimum]
        if grouping == "Largestblob" and blobs:
            blobs = [max(blobs, key=len)]
        if grouping == "Global" and blobs:
            blobs = [[point for blob in blobs for point in blob]]
        plane = np.zeros(image.shape[:2], np.float32)
        for blob in blobs:
            if view == "Selection":
                for x, y in blob:
                    plane[y, x] = 1
            else:
                plane = np.maximum(plane, _raster(_hull(blob), image.shape[:2], view, thickness))
        planes.append(plane)
    if len(planes) == 1:
        planes *= 3
    if source != "RGBA":
        if alpha == "Preserve":
            a = np.where(np.isfinite(image[:, :, 3]), np.clip(image[:, :, 3], 0, 1), 0)
        elif alpha == "Result":
            a = np.maximum.reduce(planes)
        else:
            a = np.ones(image.shape[:2], np.float32)
        planes.append(a)
    return np.stack(planes, axis=2)


def run_validation(plugin_path, parent_path="/project1", benchmark=False):
    bundle = Path(plugin_path).expanduser().resolve()
    if not bundle.is_dir():
        raise ValueError(f"Missing plugin bundle: {bundle}")
    parent_op = op(parent_path)
    if parent_op is None:
        raise ValueError(f"Missing parent COMP: {parent_path}")
    group = parent_op.create(baseCOMP, f"convex_hull_validation_{time.time_ns()}")
    callback = group.create(textDAT, "source_callbacks")
    callback.text = "def onCook(scriptOp):\n    scriptOp.copyNumpyArray(scriptOp.parent().fetch('fixture'))\n"
    rng = np.random.default_rng(4712)
    fixture = (rng.integers(0, 5, (9, 13, 4)).astype(np.float32) / 4)
    group.store("fixture", fixture)
    source = group.create(scriptTOP, "source")
    source.par.callbacks = callback
    source.par.format = "rgba32float"
    source.cook(force=True)
    native = group.create(cplusplusTOP, "hull")
    native.inputConnectors[0].connect(source)
    native.par.plugin = str(bundle)
    native.cook(force=True)
    if not hasattr(native.par, "Source"):
        raise RuntimeError(f"Plugin did not load; inspect its load message in {group.path}")
    native.par.format = "rgba32float"
    info = group.create(infoCHOP, "diagnostics")
    info.par.op = native.path

    def read():
        native.cook(force=True)
        if native.errors():
            raise RuntimeError(native.errors())
        result = native.numpyArray(delayed=False)
        if result is None:
            raise RuntimeError("Synchronous output download returned no image")
        return result.copy()

    tested = 0
    names = ("Luminance", "Red", "Green", "Blue", "Alpha", "RGB", "RGBA")
    for channel in names:
        native.par.Source = channel
        for grouping in ("Global", "Perblob", "Largestblob"):
            native.par.Grouping = grouping
            for view in ("Filled", "Outline", "Selection"):
                native.par.View = view
                np.testing.assert_array_equal(read(), _reference(fixture, source=channel, grouping=grouping, view=view))
                tested += 1
                assert native.par.Outlinewidth.enable == (view == "Outline")
                assert native.par.Outputalpha.enable == (channel != "RGBA")
                assert native.par.Thresholdmode.enable == (channel in ("RGB", "RGBA"))
    native.par.Source = "RGB"
    native.par.Grouping = "Perblob"
    native.par.View = "Outline"
    native.par.Outlinewidth = 4
    native.par.Minarea = 3
    native.par.Thresholdmode = "Independent"
    for par, value in zip(("Thresholdr", "Thresholdg", "Thresholdb", "Thresholda"), (0, .25, .75, 1)):
        setattr(native.par, par, value)
    native.par.Ignoretransparent = True
    native.par.Alphacutoff = .25
    for direction in ("Above", "Below"):
        native.par.Direction = direction
        for alpha in ("Opaque", "Preserve", "Result"):
            native.par.Outputalpha = alpha
            expected = _reference(fixture, source="RGB", grouping="Perblob", view="Outline", independent=True,
                                  below=direction == "Below", minimum=3, ignore=True, cutoff=.25, alpha=alpha, thickness=4)
            np.testing.assert_array_equal(read(), expected)
            tested += 1
    assert native.par.Thresholdr.enable and not native.par.Threshold.enable and not native.par.Thresholda.enable
    native.par.Source = "RGBA"
    np.testing.assert_array_equal(read(), _reference(fixture, source="RGBA", grouping="Perblob", view="Outline", independent=True,
                                                    below=True, minimum=3, ignore=True, cutoff=.25, alpha="Result", thickness=4))
    tested += 1
    assert native.par.Thresholda.enable and not native.par.Outputalpha.enable

    # Restore simple defaults for storage and Common-page checks.
    for par, value in {"Source": "RGB", "Grouping": "Global", "View": "Filled", "Thresholdmode": "Shared",
                       "Direction": "Above", "Minarea": 1, "Ignoretransparent": False, "Outputalpha": "Opaque"}.items():
        setattr(native.par, par, value)
    expected = _reference(fixture, source="RGB")
    native.par.outputresolution = "custom"
    native.par.resolutionw, native.par.resolutionh = 5, 3
    xs = np.floor((np.arange(5) + .5) * 13 / 5).astype(int)
    ys = np.floor((np.arange(3) + .5) * 9 / 3).astype(int)
    np.testing.assert_array_equal(read(), expected[ys[:, None], xs])
    native.par.outputaspect = "custom"
    native.par.aspect1, native.par.aspect2 = 2, 1
    read()
    assert abs(native.aspect - 2) < 1e-6
    native.par.outputaspect = "useinput"
    native.par.outputresolution = "useinput"
    formats = []
    for name in native.par.format.menuNames:
        if name == "useinput" or not any(part in name for part in ("8fixed", "16fixed", "16float", "32float", "10a2", "11float")):
            continue
        native.par.format = name
        actual = read()
        assert np.isfinite(actual).all() and actual.shape[:2] == fixture.shape[:2]
        assert np.isin(actual, [0, 1]).all(), f"Non-binary storage result in {name}"
        formats.append(name)
    native.par.format = "useinput"
    np.testing.assert_array_equal(read(), expected)
    assert native.pixelFormatName == source.pixelFormatName
    native.inputConnectors[0].disconnect()
    native.cook(force=True)
    assert native.errors()
    native.inputConnectors[0].connect(source)
    np.testing.assert_array_equal(read(), expected)
    for height, width in ((1, 1), (1, 19), (17, 1), (65, 67)):
        image = rng.random((height, width, 4), dtype=np.float32)
        group.store("fixture", image)
        source.cook(force=True)
        if width * height < 100:
            np.testing.assert_array_equal(read(), _reference(image, source="RGB"))
        else:
            assert read().shape == image.shape
        for _ in range(3):
            temporary = group.create(cplusplusTOP, "temporary")
            temporary.par.plugin = str(bundle)
            temporary.inputConnectors[0].connect(source)
            temporary.par.Source = "RGBA"
            temporary.cook(force=True)
            assert not temporary.errors(), temporary.errors()
            temporary.destroy()
    group.store("fixture", fixture)
    source.cook(force=True)
    read()
    info.cook(force=True)
    report = {"status": "passed", "touchdesigner": str(app.version), "network": group.path,
              "image_comparisons": tested, "formats": formats,
              "diagnostics": {c.name: float(c[0]) for c in info.chans()}}
    if benchmark:
        report["benchmark"] = _benchmark(group, source, native)
    group.store("fixture", fixture)
    source.cook(force=True)
    read()
    group.layoutChildren()
    destination = Path(tempfile.mkdtemp(prefix="convex-hull-host-")) / "report.json"
    destination.write_text(json.dumps(report, indent=2))
    print(f"Convex Hull host checks passed. Report: {destination}\nReview network: {group.path}")
    return report


def _benchmark(group, source, native):
    """Forced cooks on fixed synthetic inputs; report extra readback separately.

    cpuCookTime and cook() wall time do not establish GPU completion. A blocking
    numpyArray() drains output work but adds its own transfer. See README links.
    """
    native.par.format = "rgba8fixed"
    rng = np.random.default_rng(9123)
    y, x = np.indices((1080, 1920))
    results = []
    for fixture in ("solid", "shapes", "sparse", "noise"):
        if fixture == "solid":
            image = np.ones((1080, 1920, 4), np.float32)
        elif fixture == "shapes":
            image = np.stack([(((x + c * 17) % 240 > 40) & ((x + c * 17) % 240 < 170)
                               & ((y + c * 13) % 180 > 30) & ((y + c * 13) % 180 < 120)) for c in range(4)], axis=2).astype(np.float32)
        else:
            image = (rng.random((1080, 1920, 4)) < (.005 if fixture == "sparse" else .5)).astype(np.float32)
        group.store("fixture", image)
        source.cook(force=True)
        for channel in ("Luminance", "Red", "Green", "Blue", "Alpha", "RGB", "RGBA"):
            native.par.Source = channel
            for grouping in ("Global", "Perblob", "Largestblob"):
                native.par.Grouping = grouping
                samples = []
                for frame in range(15):
                    start = time.perf_counter()
                    native.cook(force=True)
                    cooked = time.perf_counter()
                    if native.errors():
                        raise RuntimeError(native.errors())
                    native.numpyArray(delayed=False)
                    finished = time.perf_counter()
                    if frame >= 3:
                        samples.append(((cooked - start) * 1000, float(native.cpuCookTime), (finished - start) * 1000))
                data = np.asarray(samples)
                results.append({"fixture": fixture, "source": channel, "grouping": grouping,
                                "cook_wall_median_ms": float(np.median(data[:, 0])),
                                "host_cpu_median_ms": float(np.median(data[:, 1])),
                                "cook_plus_readback_median_ms": float(np.median(data[:, 2])),
                                "cook_plus_readback_p95_ms": float(np.percentile(data[:, 2], 95))})
    return results
