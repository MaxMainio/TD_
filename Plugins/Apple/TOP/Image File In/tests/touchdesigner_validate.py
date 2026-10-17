"""Execute in TouchDesigner's Textport, then call run_validation().
Requires the registered AppleimagefileinTOP and ImageReaderTests fixtures.
All reports and optional project copies stay in the supplied output directory.
"""
import json
from pathlib import Path
import time
import traceback
import td


def run_validation(fixtures, output_directory):
    fixtures, root = Path(fixtures).resolve(), Path(output_directory).resolve()
    root.mkdir(parents=True, exist_ok=True)
    group = op('/').create(baseCOMP, 'image_file_in_tests')
    node = group.create('AppleimagefileinTOP', 'image')
    report = {'status': 'running', 'network': group.path, 'checks': [], 'max_cook_ms': 0,
              'fixtures': str(fixtures), 'output_directory': str(root), 'type': node.OPType}
    report_path = root / 'touchdesigner-results.json'
    report_path.write_text(json.dumps(report, indent=2))
    timeline = op('/').time
    was_playing = timeline.play
    timeline.play = False

    def check(condition, name):
        assert condition, name
        report['checks'].append(name)
        report_path.write_text(json.dumps(report, indent=2))

    def cook():
        start = time.perf_counter()
        node.cook(force=True)
        report['max_cook_ms'] = max(report['max_cook_ms'], (time.perf_counter()-start)*1000)

    def metadata():
        return next((d for d in node.docked if d.type == 'info' and d.fetch('appleImageFileInOwnerV1', '') == 'metadata'), None)

    def cell(key):
        dat = metadata()
        if dat is None: return None
        dat.cook(force=True)
        value = dat[key, 'value']
        return None if value is None else value.val

    def selector(kind):
        return next((n for n in node.docked if n.fetch('appleImageFileInOwnerV1', '') == kind), None)

    def wait_for(predicate):
        for _ in range(900):
            cook()
            if predicate():
                return
            yield
        raise AssertionError('Timed out: '+str(node.errors())+'; '+str(node.warnings()))

    def load(name):
        node.par.File = str(fixtures/name)
        # Read the cached C++ state without cooking, so a paused timeline cannot
        # be accidentally propped up by this validation harness.
        for _ in range(900):
            state = node._imageDockState()
            if state[0] == str(fixtures/name) and state[1] in ('ready', 'partial'):
                break
            yield
        else:
            raise AssertionError('Paused parameter update timed out: '+str(node.errors())+'; '+str(node.warnings()))
        check(cell('file.name') == name, 'paused update '+name)
        yield
        check(not node.errors(), 'load '+name)

    def sequence():
        for _ in range(4): cook(); yield
        check(metadata() is not None, 'metadata DAT automatically docked')
        check(node.par.format.eval() == 'rgba16float', 'float color default')
        check(all(not d.showDocked for d in node.docked), 'docked helpers collapsed by default')
        yield from load('midtone.png')
        reference = group.create(moviefileinTOP, 'color_reference')
        reference.par.file = str(fixtures/'midtone.png')
        reference.cook(force=True)
        yield
        actual, expected = node.numpyArray(delayed=False), reference.numpyArray(delayed=False)
        check(actual is not None and expected is not None and float(abs(actual-expected).max()) < .003,
              'midtone color matches native Movie File In without excess contrast')
        yield from load('portrait.heic')
        check((node.width, node.height) == (8, 16), 'native oriented color size')
        check([c.val for c in metadata().row(0)] == ['key', 'value', 'group', 'source'], 'metadata schema')
        check(selector('depth') is None, 'no invented depth selector')
        for kind in ('disparity', 'portrait', 'skin', 'hair', 'teeth', 'glasses', 'sky'):
            selected = selector(kind)
            check(selected is not None, kind+' selector automatically docked')
            selected.showDocked = True
            yield from wait_for(lambda: cell('outputs.'+kind+'.active') == 'true')
            selected.cook(force=True)
            check((selected.width, selected.height) == (2, 4), kind+' native resolution')
            selected.showDocked = False
        check(all(not d.showDocked for d in node.docked), 'all auxiliary helpers collapsed by default')
        for d in node.docked: d.showDocked = True
        check(all(d.nodeX == node.nodeX and d.nodeY < node.nodeY for d in node.docked), 'expanded helpers form a column below the TOP')
        positions = sorted((d.nodeY, d.nodeHeight) for d in node.docked)
        check(all(y+h < above[0] for (y,h),above in zip(positions,positions[1:])), 'expanded helpers do not overlap')
        for d in node.docked: d.showDocked = False
        disparity = selector('disparity')
        disparity.showDocked = True
        yield from wait_for(lambda: cell('outputs.disparity.active') == 'true')
        array = disparity.numpyArray(delayed=False)
        check(array is not None and float(array.max()) > 60, 'native disparity values preserved through Render Select')
        check(disparity.par.colorbufindex.eval() == 2, 'stable disparity buffer index')
        check(cell('outputs.disparity.available') == 'true', 'metadata and auxiliary publication match')
        node.par.outputresolution = 'custom'; node.par.resolutionw = 24; node.par.resolutionh = 32
        node.par.format = 'rgba32float'
        cook(); yield
        check((node.width, node.height) == (24, 32), 'Common custom resolution')
        disparity.cook(force=True)
        check((disparity.width, disparity.height) == (2, 4), 'Common leaves auxiliary native size')
        check(cell('outputs.color.width') == '24', 'metadata reports actual Common color size')
        node.par.outputresolution = 'useinput'
        sink = group.create(nullTOP, 'wired_disparity')
        sink.inputConnectors[0].connect(disparity)
        yield from load('color.png')
        check(selector('disparity') is disparity, 'wired selector survives file change')
        check('Unavailable' in disparity.comment, 'wired missing selector annotated')
        disparity.cook(force=True)
        # TD expands Mono32Float to RGBA for numpy; its implicit alpha is one.
        cleared = disparity.numpyArray(delayed=False)
        check(float(abs(cleared[..., :3]).max()) == 0, 'missing auxiliary output cleared')
        check(selector('portrait') is None, 'unused absent selector removed')
        yield from load('portrait.heic')
        check(selector('disparity') is disparity, 'wired selector reused when data returns')
        selected = selector('portrait'); selected.destroy()
        node.par.Refresh.pulse()
        for _ in range(60): cook(); yield
        check(selector('portrait') is None, 'deliberately deleted selector stays deleted')
        node.par.Repairoutputs.pulse(); yield; yield
        check(selector('portrait') is not None, 'repair recreates deleted selector')
        node.name = 'renamed_image'; cook(); yield
        check(disparity.par.top.eval() == node, 'selector follows renamed parent')
        check(metadata().par.op.eval() == node, 'metadata follows renamed parent')
        clone = group.copy(node)
        for _ in range(60): clone.cook(force=True); yield
        clone_dat = next((d for d in clone.docked if d.type == 'info'), None)
        check(clone_dat is not None and clone_dat.par.op.eval() == clone, 'copy has independent docked metadata')
        check(not clone.errors(), 'copied importer cooks')
        yield from load('collection.heic')
        check(len(node.par.Image.menuNames) == 3, 'collection menu contains primary plus real images')
        node.par.Image = '1'
        yield from wait_for(lambda: cell('decode.selected_image') == '1')
        check(cell('imageio.image_count') == '2', 'second HEIC image selected')
        node.par.Image = '99'
        yield from wait_for(lambda: node.par.Image.eval() == 'primary' and cell('decode.selected_image') is not None and cell('decode.selected_image') == cell('decode.primary_image'))
        check(cell('decode.selected_image') == cell('decode.primary_image'), 'unavailable image falls back to primary')
        yield from load('hdr.tiff')
        check(float(node.numpyArray(delayed=False).max()) > 1.2, 'HDR above one reaches TOP')
        node.par.Decode = 'sdr'; cook()
        yield from wait_for(lambda: cell('decode.request') == 'sdr')
        check(not node.errors(), 'explicit SDR decode')
        node.par.Decode = 'auto'
        yield from load('alpha.png')
        values = node.numpyArray(delayed=False)
        check(abs(float(values[0, 0, 3])-128/255) < .005, 'alpha reaches TOP')
        for name in ('color.jpg', 'color.tiff', 'color.gif', 'tiny.tga', 'tiny.psd', 'p3.png', 'depth.heic', 'gain-apple.heic', 'gain-mono.heic', 'gain-rgb.heic'):
            yield from load(name)
        node.par.Autorefresh = False
        node.par.File.expr = repr(str(fixtures/'color.png'))
        yield from wait_for(lambda: cell('file.name') == 'color.png')
        check(not node.errors(), 'evaluated file expression')
        node.par.File.mode = ParMode.CONSTANT
        node.par.File = str(fixtures/'broken.heic')
        yield from wait_for(lambda: bool(node.errors()))
        check(metadata().numRows <= 1, 'decode failure clears metadata')
        node.par.File = ''
        yield from wait_for(lambda: not node.errors())
        check(metadata().numRows == 1, 'empty source is idle with header')
        yield from load('portrait.heic')
        check(report['max_cook_ms'] < 500, 'no blocking file decode in host cook')
        check(not timeline.play, 'all file changes completed with timeline paused')
        timeline.play = was_playing
        report['status'] = 'passed'
        report['reload_node'] = node.path
        report['reload_width'], report['reload_height'] = node.width, node.height
        report_path.write_text(json.dumps(report, indent=2))
        print('Apple Image File In PASS:', len(report['checks']), 'checks;', report_path)

    iterator = sequence()
    def advance():
        try:
            next(iterator)
        except StopIteration:
            return
        except Exception:
            timeline.play = was_playing
            report['status'] = 'failed'; report['error'] = traceback.format_exc()
            report['node_errors'] = node.errors(); report['node_warnings'] = node.warnings()
            report_path.write_text(json.dumps(report, indent=2)); print(report['error']); return
        run('args[0]()', advance, delayFrames=1, delayRef=op.TDResources)
    advance()
    return group


def validate_reloaded(output_directory):
    root = Path(output_directory)
    report_path = root/'touchdesigner-results.json'
    report = json.loads(report_path.read_text())
    node = op(report['reload_node'])
    for_now = {'frames': 0}
    def check():
        try:
            for_now['frames'] += 1
            assert node is not None, 'Missing saved importer'
            node.cook(force=True)
            dat = next((d for d in node.docked if d.type == 'info'), None)
            if dat: dat.cook(force=True)
            disparity = next((d for d in node.docked if d.fetch('appleImageFileInOwnerV1', '') == 'disparity'), None)
            if dat and dat.numRows > 1 and disparity and not node.errors() and dat['outputs.disparity.active', 'value'] is not None and dat['outputs.disparity.active', 'value'].val == 'true':
                disparity.cook(force=True)
                assert (node.width, node.height) == (report['reload_width'], report['reload_height'])
                assert dat.par.op.eval() == node and disparity.par.top.eval() == node
                assert float(disparity.numpyArray(delayed=False).max()) > 60
                report['checks'].append('saved-project reload preserves color, metadata and auxiliary connections')
                report['reload_status'] = 'passed'
                report_path.write_text(json.dumps(report, indent=2)); print('Image File In reload PASS'); return
            assert for_now['frames'] < 900, 'Reload timed out: '+str(node.errors())
            run('args[0]()', check, delayFrames=1, delayRef=op.TDResources)
        except Exception:
            report['reload_status'] = 'failed'; report['reload_error'] = traceback.format_exc()
            report_path.write_text(json.dumps(report, indent=2)); print(report['reload_error'])
    check()
