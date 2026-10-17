"""Project model copies. Embedded per instance; TD access stays on the host thread."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
import td

_MARKER = 'appleDepthModelMenuVersion'
_PACKAGES = {
    'Smallf16': 'DepthAnythingV2SmallF16.mlpackage',
    'Smallf16p6': 'DepthAnythingV2SmallF16P6.mlpackage',
}
_run = None
_worker = None
_stop = threading.Event()
_done = threading.Event()
_result = None


def _check_cancel(cancel):
    if cancel.is_set():
        raise InterruptedError('Model copy cancelled')


def _inventory(root, cancel):
    """Hash actual package bytes, rejecting links and special files."""
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Select a complete local .mlpackage directory')
    result = {}
    for base, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            _check_cancel(cancel)
            path = Path(base) / name
            if path.is_symlink():
                raise ValueError('Model packages containing symbolic links cannot be copied')
            if path.is_dir():
                continue
            if not path.is_file():
                raise ValueError('Model package contains a special filesystem entry')
            digest = hashlib.sha256()
            with path.open('rb') as source:
                while chunk := source.read(1024 * 1024):
                    _check_cancel(cancel)
                    digest.update(chunk)
            result[path.relative_to(root).as_posix()] = digest.hexdigest()
    manifest = root / 'Manifest.json'
    if 'Manifest.json' not in result:
        raise ValueError('The .mlpackage has no Manifest.json')
    data = json.loads(manifest.read_text())
    entries = data['itemInfoEntries']
    if data['rootModelIdentifier'] not in entries:
        raise ValueError('The .mlpackage has no root model entry')
    for item in entries.values():
        relative = Path(item['path'])
        if relative.is_absolute() or '..' in relative.parts or not (root / 'Data' / relative).exists():
            raise ValueError('The .mlpackage has a missing or invalid component')
    return result


def _mkdir_unlinked(path):
    if path.is_symlink():
        raise ValueError('Packages folders must not be symbolic links')
    path.mkdir(exist_ok=True)
    if path.is_symlink() or not path.is_dir():
        raise ValueError('Packages path is not a regular directory')


def _publish(source, destination):
    # macOS renamex_np(RENAME_EXCL) publishes a directory atomically and never
    # replaces a destination created by another instance during the copy.
    libc = ctypes.CDLL(None, use_errno=True)
    rename = libc.renamex_np
    rename.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(os.fsencode(source), os.fsencode(destination), 0x00000004):
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(destination))


def copy_package(source, project_file, cancel):
    """Filesystem-only worker. The source and project identity are snapshots."""
    source, project_file = Path(source), Path(project_file)
    if source.suffix.lower() != '.mlpackage':
        raise ValueError('Copy Model to Project requires an original .mlpackage; compiled models cannot be exported as packages')
    if project_file.suffix.lower() != '.toe' or not project_file.is_file():
        raise ValueError('Save the TouchDesigner project as a .toe before copying a model')
    expected = _inventory(source, cancel)
    parent = project_file.parent / 'Packages'
    _mkdir_unlinked(parent)
    parent = parent / 'AppleDepthAnything'
    _mkdir_unlinked(parent)
    destination = parent / source.name
    if destination.exists() or destination.is_symlink():
        if _inventory(destination, cancel) != expected:
            raise FileExistsError('A different model already exists at ' + str(destination) + '; it was not overwritten')
        return destination
    with tempfile.TemporaryDirectory(prefix='.copy-', dir=parent) as temp:
        staged = Path(temp) / source.name
        staged.mkdir()
        for name in expected:
            _check_cancel(cancel)
            out = staged / name
            out.parent.mkdir(parents=True, exist_ok=True)
            with (source / name).open('rb') as src, out.open('xb') as dst:
                while chunk := src.read(1024 * 1024):
                    _check_cancel(cancel)
                    dst.write(chunk)
        if _inventory(staged, cancel) != expected:
            raise ValueError('The source model changed during copying; no package was published')
        _check_cancel(cancel)
        try:
            _publish(staged, destination)
        except FileExistsError:
            if _inventory(destination, cancel) != expected:
                raise FileExistsError('Another copy created a different model; it was not overwritten')
    return destination


def initialize(owner):
    # Existing .toe files predate Model. Keep their nonempty file selection.
    if not owner.fetch(_MARKER, False, search=False):
        if owner.par.Modelfile.eval():
            owner.par.Model = 'Custom'
        owner.store(_MARKER, True)
    # Registered C++ OP parameters reject Python enableExpr assignments.
    # getGeneralInfo() controls their enabled state through OP_Inputs.


def _selection(owner):
    return tuple((p.eval(), str(p.mode), p.expr, p.bindExpr)
                 for p in (owner.par.Model, owner.par.Modelfile))


def _project_file():
    return Path(td.project.folder) / td.project.name


def _report(owner, message):
    callback = getattr(owner, '_depthModelStatus', None)
    if callback:
        callback(message)


def _work(source, project_file):
    global _result
    try:
        _result = (str(copy_package(source, project_file, _stop)), '')
    except Exception as error:
        _result = ('', str(error))
    finally:
        _done.set()


def _later(function, *args):
    global _run
    _run = td.run('args[0](*args[1:])', function, *args,
                  delayMilliSeconds=30, delayRef=td.op.TDResources)


def dispatch(node_id, resources, copying):
    global _run, _worker, _result
    _run = None
    owner = td.op(node_id)
    if owner is None or not owner.valid or _stop.is_set():
        return
    try:
        initialize(owner)
        if not copying:
            return
        if _worker is not None:
            _report(owner, 'A model copy is already in progress')
            return
        selection = _selection(owner)
        choice = owner.par.Model.eval()
        if choice in _PACKAGES:
            source = str(Path(resources) / _PACKAGES[choice])
        elif choice == 'Custom' and owner.par.Modelfile.eval():
            source = str(owner.par.Modelfile.evalFile().absPath)
        else:
            raise ValueError('Select a model to copy')
        project_file = _project_file()
        _done.clear()
        _result = None
        _worker = threading.Thread(target=_work, args=(source, project_file), daemon=True)
        _worker.start()
        _report(owner, 'Copying model to the project Packages folder...')
        _later(poll, node_id, str(project_file), selection)
    except Exception as error:
        _report(owner, 'Copy Model to Project: ' + str(error))


def poll(node_id, project_file, selection):
    global _run, _worker
    _run = None
    owner = td.op(node_id)
    if owner is None or not owner.valid or _stop.is_set():
        shutdown()
        return
    if not _done.is_set():
        _later(poll, node_id, project_file, selection)
        return
    _worker.join()
    _worker = None
    path, error = _result
    if error:
        _report(owner, 'Copy Model to Project: ' + error)
        return
    # A completed old request must not replace a newer user selection or a
    # project switched/saved elsewhere while the worker was running.
    if str(_project_file()) != project_file or _selection(owner) != selection:
        _report(owner, 'Model copied to ' + path + '; the current selection was preserved because it changed during copying')
        return
    owner.par.Modelfile = Path(path).relative_to(Path(project_file).parent).as_posix()
    owner.par.Model = 'Custom'
    _report(owner, '')
    owner.cook(force=True)


def schedule(node_id, resources, copying=False):
    if _worker is not None:
        owner = td.op(node_id)
        if owner is not None and owner.valid:
            _report(owner, 'A model copy is already in progress')
        return
    global _run
    if _run is not None:
        _run.kill()
    _later(dispatch, node_id, resources, copying)


def shutdown():
    global _run, _worker
    _stop.set()
    if _run is not None:
        try:
            _run.kill()
        except Exception:
            pass  # The host may already have invalidated a completed run.
        _run = None
    if _worker is not None:
        _worker.join()
        _worker = None
