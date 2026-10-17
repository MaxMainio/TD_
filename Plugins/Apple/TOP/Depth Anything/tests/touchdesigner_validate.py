"""Run from TD's Textport in a saved workspace validation project.

The project-local Plugins folder must contain AppleDepthAnything.plugin.
Call run_validation(output_directory); results are written there.
"""
import json
from pathlib import Path
import traceback
import numpy as np
import td


def run_validation(output_directory):
    root = Path(output_directory).resolve()
    if Path(td.project.folder).resolve() != root:
        raise ValueError('Save a test .toe in the supplied output directory first')
    group = td.op('/').create(td.baseCOMP, 'depth_anything_tests')
    source = group.create(td.noiseTOP, 'image')
    node = group.create('AppledepthanythingTOP')
    node.inputConnectors[0].connect(source)
    node.viewer = True
    report = {'status': 'running', 'checks': [], 'network': group.path,
              'project': td.project.name, 'models': {}}
    path = root / 'touchdesigner-results.json'
    timeline = td.op('/').time
    previous_play = timeline.play
    timeline.play = False

    def save(): path.write_text(json.dumps(report, indent=2))
    def check(value, message):
        if not value: raise AssertionError(message)
        report['checks'].append(message); save()
    def frames(count=3):
        for _ in range(count): yield
    def pixels():
        node.cook(force=True)
        check(not node.errors(), 'inference without errors: ' + node.par.Model.eval())
        data = node.numpyArray(delayed=False)
        check(data is not None and np.isfinite(data).all() and float(np.ptp(data[..., 0])) > .1,
              'finite nonconstant output: ' + node.par.Model.eval())
        return data.copy()
    def wait_copy(name):
        expected = 'Packages/AppleDepthAnything/' + name
        for _ in range(600):
            if node.par.Model.eval() == 'Custom' and node.par.Modelfile.eval() == expected:
                break
            yield
        else: raise AssertionError('Copy timeout: ' + str(node.warnings()))
        check((root / expected / 'Manifest.json').is_file(), 'package copy: ' + name)
        check(node.par.Modelfile.enable, 'Custom file enabled after copy')
        pixels()

    def sequence():
        yield from frames()
        check(node.par.Model.menuNames == ['Smallf16', 'Smallf16p6', 'Custom'], 'model menu choices')
        check(node.par.Model.eval() == 'Smallf16', 'default bundled F16')
        check(not node.par.Modelfile.enable, 'file disabled for bundled F16 while paused')
        first = pixels()
        report['models']['Smallf16'] = {'shape': list(first.shape)}
        for menu, name in [('Smallf16', 'DepthAnythingV2SmallF16.mlpackage'),
                           ('Smallf16p6', 'DepthAnythingV2SmallF16P6.mlpackage')]:
            node.par.Model = menu
            yield from frames()
            check(not node.par.Modelfile.enable, 'file disabled for ' + menu)
            data = pixels(); report['models'][menu] = {'shape': list(data.shape)}
            node.par.Copymodel.pulse()
            yield from wait_copy(name)
            model_file = root / node.par.Modelfile.eval() / 'Manifest.json'
            timestamp = model_file.stat().st_mtime_ns
            node.par.Copymodel.pulse()
            yield from frames(30)
            check(not node.warnings(), 'identical project copy succeeds without warning')
            check(model_file.stat().st_mtime_ns == timestamp, 'identical model reused without rewrite')
        retained = node.par.Modelfile.eval()
        node.par.Model = 'Smallf16'
        yield from frames()
        check(node.par.Modelfile.eval() == retained and not node.par.Modelfile.enable,
              'switching to bundled model preserves disabled custom path')
        node.par.Model = 'Custom'
        node.par.Modelfile = 'Packages/missing.mlpackage'
        yield from frames()
        node.cook(force=True)
        check(bool(node.errors()), 'missing custom model reports node error')
        check(np.max(np.abs(node.numpyArray(delayed=False))) == 0, 'missing model clears output')
        node.par.Model = 'Smallf16'
        yield from frames(); pixels()
        node.name = 'depth_anything_renamed'
        node.par.Copymodel.pulse()
        yield from wait_copy('DepthAnythingV2SmallF16.mlpackage')
        check(not node.warnings(), 'copy follows renamed node')
        duplicate = group.copy(node)
        yield from frames()
        check(duplicate.par.Model.eval() == 'Custom' and duplicate.par.Modelfile.eval() == node.par.Modelfile.eval(),
              'node duplication preserves project-relative selection')
        duplicate.destroy()
        report['status'] = 'passed'
        # Save the validation project in place with the relative path selected.
        td.project.save(str(root / 'DepthAnythingValidated.toe'))
        report['saved_project'] = td.project.name
        save()
        print('Apple Depth Anything live checks passed:', len(report['checks']))

    steps = sequence()
    def tick():
        try:
            next(steps)
            td.run('args[0]()', tick, delayMilliSeconds=30, delayRef=td.op.TDResources)
        except StopIteration:
            timeline.play = previous_play
        except Exception:
            report['status'], report['error'] = 'failed', traceback.format_exc()
            save(); timeline.play = previous_play
            print(report['error'])
    save(); tick()
    return group.path
