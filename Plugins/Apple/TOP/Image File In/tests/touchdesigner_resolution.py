"""Run run_resolution_validation(fixtures, output_directory) in Textport.
Uses a dedicated component and never force-cooks the importer to advance it.
"""
import json
from pathlib import Path
import time
import traceback


def run_resolution_validation(fixtures, output_directory):
    fixtures, root = Path(fixtures).resolve(), Path(output_directory).resolve()
    root.mkdir(parents=True, exist_ok=True)
    group = op('/').create(baseCOMP, 'apple_image_resolution_tests')
    node = group.create('AppleimagefileinTOP', 'image')
    timeline = op('/').time
    was_playing = timeline.play
    timeline.play = False
    report = {'status': 'running', 'checks': [], 'node': node.path,
              'enable_states': {},
              'menu': dict(zip(node.par.outputresolution.menuNames, node.par.outputresolution.menuLabels))}
    report_path = root/'resolution-results.json'

    def save():
        report_path.write_text(json.dumps(report, indent=2))

    def check(condition, name):
        assert condition, name
        report['checks'].append(name)
        save()

    def helper(kind):
        return next((d for d in node.docked if d.fetch('appleImageFileInOwnerV1', '') == kind), None)

    def cell(key):
        dat = helper('metadata')
        if dat is None:
            return None
        dat.cook(force=True)
        value = dat[key, 'value']
        return value.val if value is not None else None

    def wait(predicate, label):
        start = time.monotonic()
        while not predicate():
            assert time.monotonic()-start < 15, label+': '+node.errors()+'; '+node.warnings()
            yield
        check(True, label)

    def settle():
        end = time.monotonic()+.15
        while time.monotonic() < end:
            yield

    def sequence():
        node.par.File = str(fixtures/'portrait-large-mattes.heic')
        node.par.Auxiliaryoutputs = 'all'
        yield from wait(lambda: cell('outputs.sky.active') == 'true', 'mixed-size auxiliaries load while paused')
        check((node.width, node.height) == (8, 16), 'native oriented color size')
        kinds = ('disparity', 'portrait', 'skin', 'hair', 'teeth', 'glasses', 'sky')
        yield from wait(lambda: all(helper(k) is not None for k in kinds), 'deferred auxiliary docking completes')
        native = {k: (int(cell('outputs.'+k+'.native_width')), int(cell('outputs.'+k+'.native_height'))) for k in kinds}
        check(native['disparity'] == (2, 4) and native['portrait'] == (4, 8), 'disparity smaller than other maps')
        original = {k: helper(k).numpyArray(delayed=False).copy() for k in kinds}
        read_ms, prepared = cell('decode.read_ms'), cell('decode.auxiliary_preparations')
        # All Common choices must expose exactly the applicable resolution fields.
        for mode in report['menu']:
            node.par.outputresolution = mode
            yield from settle()
            expected = mode in ('custom', 'fit', 'limit')
            report['enable_states'][mode] = {'expected': expected,
                'width': bool(node.par.resolutionw.enable), 'height': bool(node.par.resolutionh.enable)}
            save()
        for mode, bounds, color_size in (
                ('useinput', (11, 13), (8, 16)), ('half', (11, 13), (4, 8)),
                ('quarter', (11, 13), (2, 4)), ('eighth', (11, 13), (1, 2)),
                ('2x', (11, 13), (16, 32)), ('4x', (11, 13), (32, 64)),
                ('8x', (11, 13), (64, 128)), ('custom', (1, 20), (1, 20)),
                ('custom', (20, 2), (20, 2)), ('fit', (4, 4), (2, 4)),
                ('limit', (4, 4), (2, 4)), ('limit', (32, 32), (8, 16))):
            node.par.resolutionw, node.par.resolutionh = bounds
            node.par.outputresolution = mode
            yield from wait(lambda: (int(cell('outputs.color.width') or 0), int(cell('outputs.color.height') or 0)) == color_size,
                            mode+' '+str(bounds)+' resolves main output')
            for kind in kinds:
                expected = tuple(min(a, b) for a, b in zip(native[kind], color_size))
                image = helper(kind).numpyArray(delayed=False)
                check((image.shape[1], image.shape[0]) == expected, mode+' '+str(bounds)+' caps '+kind)
                check((int(cell('outputs.'+kind+'.width')), int(cell('outputs.'+kind+'.height'))) == expected,
                      mode+' '+str(bounds)+' metadata matches '+kind)
                if kind == 'disparity':
                    check(float(image[..., 0].max()) > 1, 'resized disparity retains native units')
            check(cell('decode.read_ms') == read_ms and cell('decode.auxiliary_preparations') == prepared,
                  mode+' reuses source and prepared maps')
        node.par.outputresolution = 'useinput'
        yield from wait(lambda: cell('outputs.color.width') == '8', 'native output restored')
        for kind in kinds:
            check(float(abs(helper(kind).numpyArray(delayed=False)-original[kind]).max()) == 0,
                  'native pixels restored without cumulative resampling '+kind)
        node.par.Auxiliaryoutputs = 'automatic'
        yield from wait(lambda: cell('outputs.portrait.state') == 'inactive', 'unused maps suspend after resizing')
        node.par.outputresolution = 'quarter'
        yield from wait(lambda: cell('outputs.color.width') == '2', 'inactive resize completes while paused')
        check(cell('outputs.portrait.width') == '2' and cell('outputs.portrait.native_width') == '4',
              'inactive metadata keeps capped and native dimensions')
        helper('portrait').showDocked = True
        yield from wait(lambda: cell('outputs.portrait.active') == 'true', 'resume respects current cap while paused')
        check(helper('portrait').numpyArray(delayed=False).shape[:2] == (4, 2), 'resumed map has capped size')
        node.par.Auxiliaryoutputs = 'all'
        node.par.outputresolution = 'custom'
        node.par.resolutionw = node.par.resolutionh = 1
        for filename, kind in (('depth.heic', 'depth'), ('gain-apple.heic', 'hdr_gain'),
                               ('gain-mono.heic', 'iso_gain'), ('gain-rgb.heic', 'iso_gain')):
            node.par.File = str(fixtures/filename)
            yield from wait(lambda: cell('file.name') == filename and cell('outputs.'+kind+'.active') == 'true' and helper(kind) is not None,
                            filename+' capped output ready')
            data = helper(kind).numpyArray(delayed=False)
            check(data.shape[:2] == (1, 1), filename+' native channels downsample to cap')
            check(cell('outputs.'+kind+'.width') == '1' and cell('outputs.'+kind+'.height') == '1',
                  filename+' metadata matches capped size')
        check(not timeline.play, 'all parameter and buffer transitions completed while paused')
        report['resolution_enable_status'] = 'host-managed; built-in override unsupported in 2025.33230'
        report['status'] = 'passed'
        save()
        timeline.play = was_playing
        print('Apple Image In resolution PASS:', len(report['checks']), 'checks')

    save()
    iterator = sequence()
    def advance():
        try:
            next(iterator)
        except StopIteration:
            return
        except Exception:
            timeline.play = was_playing
            report['status'] = 'failed'
            report['error'] = traceback.format_exc()
            save()
            print(report['error'])
            return
        run('args[0]()', advance, delayMilliSeconds=16, delayRef=op.TDResources)
    advance()
    return group
