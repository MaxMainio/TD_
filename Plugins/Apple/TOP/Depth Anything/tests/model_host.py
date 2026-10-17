"""Focused filesystem and deferred-host tests; no TouchDesigner acceptance claim."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import types
import unittest

ROOT = Path(sys.argv.pop(1)).resolve()
ROOT.mkdir(parents=True, exist_ok=True)
td = types.ModuleType('td')
sys.modules['td'] = td
spec = importlib.util.spec_from_file_location('model_host', Path(__file__).parents[1] / 'ModelHost.py')
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


def package(path):
    (path / 'Data/model').mkdir(parents=True)
    (path / 'Data/model/spec.mlmodel').write_bytes(b'test-model')
    (path / 'Data/model/weights.bin').write_bytes(b'weights' * 1000)
    (path / 'Manifest.json').write_text(json.dumps({
        'rootModelIdentifier': 'model',
        'itemInfoEntries': {'model': {'path': 'model/spec.mlmodel'}}}))
    return path


class Par:
    def __init__(self, value):
        self.value, self.mode, self.expr, self.bindExpr = value, 'constant', '', ''
    @property
    def enableExpr(self): return None
    @enableExpr.setter
    def enableExpr(self, value):
        raise RuntimeError('Registered C++ parameters reject enableExpr')
    def eval(self): return self.value
    def evalFile(self): return types.SimpleNamespace(absPath=self.value)


class Pars:
    def __init__(self):
        self.Model, self.Modelfile = Par('Smallf16'), Par('')
    def __setattr__(self, name, value):
        if name in self.__dict__ and not isinstance(value, Par):
            self.__dict__[name].value = value
        else:
            object.__setattr__(self, name, value)


class Owner:
    valid = True
    def __init__(self): self.par, self.storage, self.messages, self.cooks = Pars(), {}, [], 0
    def fetch(self, key, default, **kwargs): return self.storage.get(key, default)
    def store(self, key, value): self.storage[key] = value
    def _depthModelStatus(self, value): self.messages.append(value)
    def cook(self, **kwargs): self.cooks += 1


class Copies(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT)
        self.root = Path(self.temp.name)
        self.source = package(self.root / 'source' / 'Example.mlpackage')
        self.project = self.root / 'project space' / 'Example.toe'
        self.project.parent.mkdir()
        self.project.write_text('saved-project-fixture')
        self.cancel = threading.Event()
    def tearDown(self): self.temp.cleanup()
    def copy(self): return host.copy_package(self.source, self.project, self.cancel)
    def test_copy_and_reuse(self):
        destination = self.copy()
        self.assertEqual(destination, self.project.parent / 'Packages/AppleDepthAnything/Example.mlpackage')
        self.assertEqual(host._inventory(self.source, self.cancel), host._inventory(destination, self.cancel))
        original = (destination / 'Manifest.json').stat().st_mtime_ns
        self.assertEqual(self.copy(), destination)
        self.assertEqual((destination / 'Manifest.json').stat().st_mtime_ns, original)
    def test_conflict_does_not_overwrite(self):
        destination = self.copy()
        weights = destination / 'Data/model/weights.bin'
        weights.write_bytes(b'user model')
        with self.assertRaises(FileExistsError): self.copy()
        self.assertEqual(weights.read_bytes(), b'user model')
    def test_unsaved_project(self):
        self.project.unlink()
        with self.assertRaisesRegex(ValueError, 'Save'): self.copy()
        self.assertFalse((self.project.parent / 'Packages').exists())
    def test_source_link(self):
        (self.source / 'Data/external').symlink_to(self.project)
        with self.assertRaisesRegex(ValueError, 'links'): self.copy()
    def test_destination_link(self):
        (self.project.parent / 'Packages').symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'links'): self.copy()
    def test_invalid_package_component(self):
        (self.source / 'Data/model/spec.mlmodel').unlink()
        with self.assertRaises(ValueError): self.copy()
    def test_cancellation(self):
        self.cancel.set()
        with self.assertRaises(InterruptedError): self.copy()
        self.assertFalse((self.project.parent / 'Packages').exists())
    def test_failed_copy_is_not_published(self):
        original = host._publish
        host._publish = lambda *_: (_ for _ in ()).throw(PermissionError('read-only'))
        try:
            with self.assertRaises(PermissionError): self.copy()
            self.assertEqual(list((self.project.parent / 'Packages/AppleDepthAnything').iterdir()), [])
        finally: host._publish = original
    def test_concurrent_identical_publish(self):
        original = host._publish
        def concurrent(source, dest):
            shutil.copytree(source, dest)
            original(source, dest)
        host._publish = concurrent
        try: self.assertTrue(self.copy().is_dir())
        finally: host._publish = original
    def test_compiled_model_not_exported_as_package(self):
        self.source = self.source.rename(self.source.with_suffix('.mlmodelc'))
        with self.assertRaisesRegex(ValueError, 'original .mlpackage'): self.copy()
    def test_existing_custom_path_migrates_once(self):
        owner = Owner()
        owner.par.Modelfile = 'old/model.mlpackage'
        host.initialize(owner)
        self.assertEqual(owner.par.Model.eval(), 'Custom')
        owner.par.Model = 'Smallf16p6'
        host.initialize(owner)
        self.assertEqual(owner.par.Model.eval(), 'Smallf16p6')
        self.assertEqual(owner.par.Modelfile.eval(), 'old/model.mlpackage')
    def test_default_and_relative_completion(self):
        owner = Owner()
        host.initialize(owner)
        self.assertEqual(owner.par.Model.eval(), 'Smallf16')
        dest = self.copy()
        td.op = lambda _: owner
        td.project = types.SimpleNamespace(folder=str(self.project.parent), name=self.project.name)
        host._worker = types.SimpleNamespace(join=lambda: None)
        host._done.set(); host._stop.clear(); host._result = (str(dest), '')
        host.poll(1, str(self.project), host._selection(owner))
        self.assertEqual(owner.par.Model.eval(), 'Custom')
        self.assertEqual(owner.par.Modelfile.eval(), 'Packages/AppleDepthAnything/Example.mlpackage')
        self.assertEqual(owner.cooks, 1)
    def test_late_completion_preserves_new_selection(self):
        owner = Owner()
        selection = host._selection(owner)
        owner.par.Model = 'Smallf16p6'
        td.op = lambda _: owner
        td.project = types.SimpleNamespace(folder=str(self.project.parent), name=self.project.name)
        host._worker = types.SimpleNamespace(join=lambda: None)
        host._done.set(); host._stop.clear(); host._result = (str(self.copy()), '')
        host.poll(1, str(self.project), selection)
        self.assertEqual(owner.par.Model.eval(), 'Smallf16p6')
        self.assertEqual(owner.cooks, 0)
    def test_shutdown_cancels_pending_work(self):
        killed = []
        host._run = types.SimpleNamespace(kill=lambda: killed.append(True))
        host._worker = None
        host.shutdown()
        self.assertTrue(host._stop.is_set() and killed)
        host._stop.clear()


unittest.main()
