"""Run in TouchDesigner's Textport with exec(), then call run_validation().

All fixtures/reports stay in the supplied output directory. Each invocation
creates a uniquely named Base COMP and yields between frames while waiting for
background extraction. It leaves the network available for inspection.
"""
import json
import os
from pathlib import Path
import time
import traceback
import wave


def run_validation(plugin_path, output_directory, parent_path="/", custom_type=None):
    root = Path(output_directory).resolve()
    root.mkdir(parents=True, exist_ok=True)
    bundle = Path(plugin_path).resolve()
    assert bundle.is_dir(), str(bundle)
    group = op(parent_path).create("baseCOMP", "metadata_validation_" + str(time.time_ns()))
    native = group.create(custom_type or "cplusplusDAT", "metadata")
    if not custom_type:
        native.par.plugin = str(bundle)
    native.cook(force=True)
    assert hasattr(native.par, "Deepscan"), "Plugin failed to load: " + str(native.errors())
    assert not hasattr(native.par, "Source") and not hasattr(native.par, "Fileparameter"), "Old parameter interface loaded"
    native.par.Deepscan = True
    native.par.Checkinterval = .1
    info = group.create("infoDAT", "diagnostics")
    info.par.op = native.path
    infochop = group.create("infoCHOP", "channels")
    infochop.par.op = native.path
    downstream = group.create("nullDAT", "downstream")
    downstream.inputConnectors[0].connect(native)
    a, b = root / "points.csv", root / "second.xyz"
    a.write_text("x,y,z\n1,2,3\n4,5,6\n")
    b.write_text("9 8 7\n")
    mesh = root / "mesh.obj"
    mesh.write_text("v 1 2 3\nv 4 5 6\nf 1 2 1\n")
    ply = root / "points.ply"
    ply.write_text("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n1 2 3\n")
    wav = root / "sound.wav"
    with wave.open(str(wav), "wb") as f:
        f.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        f.writeframes(b"\0" * 1600)
    report = {"status": "running", "network": group.path, "operator_type": native.OPType,
              "touchdesigner": str(app.version) + "." + str(app.build), "checks": [], "max_cook_ms": 0}
    report_path = root / "touchdesigner-results.json"

    def cook():
        start = time.perf_counter()
        native.cook(force=True)
        report["max_cook_ms"] = max(report["max_cook_ms"], (time.perf_counter()-start)*1000)

    def cell(node, key):
        c = node[key, "value"]
        return c.val if c is not None else None

    def diagnostic(key):
        info.cook(force=True)
        c = info[key, 1]
        return c.val if c is not None else None

    def record(name):
        report["checks"].append(name)
        report_path.write_text(json.dumps(report, indent=2))

    def wait_for(predicate, label, seconds=10):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            cook()
            if predicate():
                record(label)
                return
            yield
        raise AssertionError(label + ": " + str(native.errors()) + "; " + str(diagnostic("status"))
                             + "; shape=" + str((native.numRows, native.numCols)))

    def pause(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            cook()
            yield

    def scenario():
        assert native.par.File.enable and native.par.Operator.enable
        assert diagnostic("status") == "idle"
        assert [p.name for p in native.pars() if p.name[0].isupper()] == ["File", "Operator", "Autorefresh", "Refresh", "Deepscan", "Checkinterval", "Readtimeout"]
        assert all(str(getattr(native.par, name).page) == "Source" for name in ("File", "Operator", "Autorefresh", "Refresh", "Deepscan"))
        assert all(str(getattr(native.par, name).page) == "Technical" for name in ("Checkinterval", "Readtimeout",))
        record("two-input interface, main controls and Technical page")
        native.par.File = str(a)
        yield from wait_for(lambda: cell(native, "geometry.calculated.point_count") == "2", "direct file")
        assert native.par.File.enable and not native.par.Operator.enable
        record("File automatically disables Operator")
        assert [native[0, c].val for c in range(4)] == ["key", "value", "group", "source"]
        keys = [native[row, 0].val for row in range(1, native.numRows)]
        assert keys == sorted(keys) and len(keys) == len(set(keys))
        downstream.cook(force=True)
        assert cell(downstream, "file.name") == a.name
        record("downstream row lookup and table schema")
        reads = diagnostic("reads")
        yield from pause(.4)
        assert diagnostic("reads") == reads
        assert cell(native, "file.name") == a.name
        record("unchanged file cached across cooks")
        native.par.Refresh.pulse()
        yield from wait_for(lambda: diagnostic("reads") != reads and cell(native, "file.name") == a.name, "Refresh bypasses cache")
        a.write_text("x,y,z\n1,2,3\n")
        yield from wait_for(lambda: cell(native, "geometry.calculated.point_count") == "1", "automatic disk change")
        # Rename-over replacement has a different file identity.
        replacement = root / "replacement.csv"
        replacement.write_text("x,y,z\n3,4,5\n6,7,8\n")
        replacement.replace(a)
        yield from wait_for(lambda: cell(native, "geometry.calculated.point_count") == "2", "file replacement")
        native.par.File = str(b)
        cook()
        assert cell(native, "file.name") in (None, b.name)
        yield from wait_for(lambda: cell(native, "file.name") == b.name, "source change clears previous rows")
        native.par.File = ""
        cook()
        assert native.par.File.enable and native.par.Operator.enable and not native.errors()
        assert cell(native, "file.path") is None
        record("clearing source re-enables both inputs")
        for kind, file in (("moviefileinTOP", wav), ("fileinCHOP", a), ("fileinSOP", mesh), ("pointfileinPOP", ply)):
            source = group.create(kind, "source_" + kind.lower())
            matches = [p for p in source.pars() if p.name.lower() == "file"]
            assert len(matches) == 1, (kind, [p.name for p in source.pars()])
            matches[0].val = str(file)
            native.par.Operator = source
            yield from wait_for(lambda file=file: cell(native, "file.path") == str(file), "generic " + kind)
            assert not native.par.File.enable and native.par.Operator.enable
        record("Operator automatically disables File")
        source = group.create("baseCOMP", "custom_source")
        page = source.appendCustomPage("Source")
        filepar = page.appendFile("File")[0]
        filepar.val = os.path.relpath(a, project.folder)
        native.par.Operator = source
        yield from wait_for(lambda: cell(native, "file.path") == filepar.evalFile().absPath, "custom COMP and relative path")
        filepar.expr = repr(str(b))
        yield from wait_for(lambda: cell(native, "file.path") == str(b), "source expression")
        master = page.appendFile("Master")[0]
        master.val = str(a)
        filepar.bindExpr = "me.par.Master"
        filepar.mode = ParMode.BIND
        yield from wait_for(lambda: cell(native, "file.path") == str(a), "source binding")
        native.par.Operator.expr = "op(" + repr(source.path) + ")"
        yield from wait_for(lambda: cell(native, "file.path") == str(a), "operator reference expression")
        assert not native.par.File.enable and native.par.Operator.enable
        source.name = "renamed_custom_source"
        native.par.Operator = source
        yield from wait_for(lambda: cell(native, "file.path") == str(a), "renamed operator reference")
        source.destroy()
        yield from wait_for(lambda: bool(native.errors()) and native.numRows <= 1, "deleted source clears rows")
        ambiguous = group.create("baseCOMP", "ambiguous")
        page = ambiguous.appendCustomPage("Source")
        page.appendFile("A", label="File")[0].val = str(a)
        page.appendFile("B", label="File")[0].val = str(b)
        native.par.Operator = ambiguous
        yield from wait_for(lambda: bool(native.errors()) and native.numRows <= 1, "ambiguous File labels rejected")
        unambiguous = group.create("baseCOMP", "label_source")
        unambiguous.appendCustomPage("Source").appendFile("Path", label="File")[0].val = str(b)
        native.par.Operator = unambiguous
        yield from wait_for(lambda: cell(native, "file.path") == str(b), "automatic File label detection")
        native.par.File = str(a)
        yield from wait_for(lambda: bool(native.errors()) and native.numRows <= 1, "conflicting sources rejected")
        assert native.par.File.enable and native.par.Operator.enable
        native.par.File = ""
        yield from wait_for(lambda: cell(native, "file.path") == str(b), "conflict can be cleared")
        native.par.Operator = [ambiguous, group]
        yield from wait_for(lambda: bool(native.errors()) and native.numRows <= 1, "multiple operators rejected")
        native.par.Operator = ""
        cook()
        assert native.par.File.enable and native.par.Operator.enable
        for bad in (str(root), str(root / "frame*.png"), "https://example.invalid/a", "vfs:/invalid:a"):
            native.par.File = bad
            yield from wait_for(lambda: bool(native.errors()) and native.numRows <= 1, "reject " + bad)
        native.par.File = str(b)
        yield from wait_for(lambda: cell(native, "file.path") == str(b), "valid after errors")
        b.unlink()
        yield from wait_for(lambda: bool(native.errors()) and native.numRows <= 1, "deleted file")
        b.write_text("9 8 7\n")
        yield from wait_for(lambda: cell(native, "file.path") == str(b), "file recovery")
        for i in range(30):
            native.par.File = str(a if i % 2 else b)
            cook()
        yield from wait_for(lambda: cell(native, "file.path") == str(a), "rapid source switching")
        infochop.cook(force=True)
        assert infochop["rows"] is not None
        assert diagnostic("exiftool_version") and diagnostic("ffprobe_version")
        record("Info DAT and CHOP")
        native.par.Autorefresh = False
        cook()
        assert not native.par.Checkinterval.enable and native.par.Readtimeout.enable
        native.par.Autorefresh = True
        cook()
        assert native.par.Checkinterval.enable
        record("Technical check interval enable state")
        # A large valid OBJ keeps the worker busy without blocking the host.
        slow = root / "slow.obj"
        with slow.open("w") as f:
            chunk = "v 1 2 3\n" * 10000
            for _ in range(300):
                f.write(chunk)
        native.par.File = str(slow)
        yield from pause(.15)
        native.par.File = str(a)
        yield from wait_for(lambda: cell(native, "file.path") == str(a), "supersede slow scan")
        doomed = group.create(custom_type or "cplusplusDAT", "destroy_during_read")
        if not custom_type:
            doomed.par.plugin = str(bundle)
        doomed.cook(force=True)
        doomed.par.File = str(slow)
        doomed.par.Deepscan = True
        doomed.cook(force=True)
        yield
        start = time.perf_counter()
        doomed.destroy()
        assert time.perf_counter()-start < 2, "slow destruction"
        record("node destruction during extraction")
        group.layout()
        assert report["max_cook_ms"] < 500, report["max_cook_ms"]
        record("responsive cooks during slow scan")
        # Retain the resolved file and downstream row for project-reload validation.
        report["reload_expected_file"] = str(a)
        report["status"] = "passed"

    task = scenario()

    def tick():
        try:
            next(task)
        except StopIteration:
            report_path.write_text(json.dumps(report, indent=2))
            print("MetaData_In PASS:", len(report["checks"]), "checks;", report_path)
            return
        except Exception:
            report["status"] = "failed"
            report["error"] = traceback.format_exc()
            report_path.write_text(json.dumps(report, indent=2))
            print(report["error"])
            return
        run("args[0]()", tick, delayFrames=1)

    tick()
    return group


def validate_reloaded(output_directory):
    """Call after saving and reopening the .toe created during run_validation."""
    path = Path(output_directory) / "touchdesigner-results.json"
    report = json.loads(path.read_text())
    node = op(report["network"] + "/metadata")
    assert node is not None, "Saved validation node missing"
    started = time.monotonic()

    def check():
        node.cook(force=True)
        cell = node["file.path", "value"]
        if cell is not None and cell.val == report["reload_expected_file"]:
            downstream = op(report["network"] + "/downstream")
            downstream.cook(force=True)
            assert downstream["file.path", "value"].val == cell.val
            report["checks"].append("project reload and downstream row lookup")
            path.write_text(json.dumps(report, indent=2))
            print("MetaData_In project reload PASS")
            verify_registered()
        elif time.monotonic() - started < 10:
            run("args[0]()", check, delayFrames=1)
        else:
            report["status"] = "failed"
            report["error"] = "Project reload timed out: " + str(node.errors())
            path.write_text(json.dumps(report, indent=2))
            print(report["error"])

    def verify_registered():
        group = node.parent()
        custom = None
        errors = []
        for kind in ("MetadatainDAT", "metadatainDAT", "Metadatain"):
            try:
                custom = group.create(kind, "registered_metadata")
                break
            except Exception as error:
                errors.append(str(error))
        if custom is None:
            report["registered_creation"] = {"status": "unverified", "errors": errors}
            path.write_text(json.dumps(report, indent=2))
            print("Registered Custom DAT creation needs manual validation:", errors)
            return
        custom.par.File = report["reload_expected_file"]
        custom.par.Deepscan = True
        began = time.monotonic()

        def check_custom():
            custom.cook(force=True)
            cell = custom["file.path", "value"]
            if cell is not None and cell.val == report["reload_expected_file"]:
                report["registered_creation"] = {"status": "passed", "type": custom.OPType,
                                                  "label": custom.label, "inputs": custom.minInputs}
                report["checks"].append("registered Custom DAT creation and cooking")
                path.write_text(json.dumps(report, indent=2))
                group.layout()
                print("MetaData_In registered Custom DAT PASS")
            elif time.monotonic()-began < 10:
                run("args[0]()", check_custom, delayFrames=1)
            else:
                report["registered_creation"] = {"status": "failed", "error": str(custom.errors())}
                path.write_text(json.dumps(report, indent=2))
                print("MetaData_In registered Custom DAT FAILED", custom.errors())
        check_custom()
    check()


def save_and_reload_validation(output_directory, script_path):
    """Explicit final host check: save a test .toe beside output_directory and reopen it."""
    root = Path(output_directory).resolve()
    report = json.loads((root / "touchdesigner-results.json").read_text())
    assert report["status"] == "passed", "Complete run_validation first"
    group = op(report["network"])
    callback = group.create("executeDAT", "reload_validation")
    callback.par.active = False
    callback.text = ("def onStart():\n"
                     "    scope = globals()\n"
                     "    exec(compile(open(" + repr(str(script_path)) + ").read(), " + repr(str(script_path)) + ", 'exec'), scope, scope)\n"
                     "    run('args[0](args[1])', scope['validate_reloaded'], " + repr(str(root)) + ", delayFrames=10)\n")
    for name in ("create", "exit", "framestart", "frameend", "playstatechange", "devicechange"):
        getattr(callback.par, name).val = False
    callback.par.start = True
    callback.par.active = True
    target = root.parent / ("metadata-reload-" + str(time.time_ns()) + ".toe")
    project.save(str(target), saveExternalToxs=False)
    project.load(str(target))
