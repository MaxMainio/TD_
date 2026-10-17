"""Run in TouchDesigner's Textport. Reports stay in output_directory.
The test reads cached metadata but never force-cooks the importer to advance it.
"""
import json
from pathlib import Path
import time
import traceback


def run_demand_validation(fixtures, output_directory):
    fixtures, root = Path(fixtures).resolve(), Path(output_directory).resolve()
    root.mkdir(parents=True, exist_ok=True)
    group = op('/').create(baseCOMP, 'apple_image_demand_tests')
    node = group.create('AppleimagefileinTOP', 'image')
    timeline = op('/').time
    was_playing = timeline.play
    timeline.play = False
    report = {'status': 'running', 'checks': [], 'node': node.path}
    report_path = root / 'demand-results.json'
    report_path.write_text(json.dumps(report, indent=2))

    def check(condition, name):
        assert condition, name
        report['checks'].append(name)
        report_path.write_text(json.dumps(report, indent=2))

    def helper(kind):
        return next((d for d in node.docked if d.fetch('appleImageFileInOwnerV1', '') == kind), None)

    def cell(key):
        dat = helper('metadata')
        if dat is None:
            return None
        dat.cook(force=True)
        value = dat[key, 'value']
        return None if value is None else value.val

    def wait(predicate, label, timeout=15):
        start = time.monotonic()
        while not predicate():
            assert time.monotonic()-start < timeout, label+': '+node.errors()+'; '+node.warnings()
            yield
        check(True, label)

    def settle(seconds=1.5):
        end = time.monotonic()+seconds
        while time.monotonic() < end:
            yield

    def sequence():
        # cookOnStart initiates the independent callback; File subsequently
        # changes only through normal parameters while the timeline is paused.
        node.par.File = str(fixtures/'portrait.heic')
        yield from wait(lambda: helper('disparity') is not None, 'paused load discovers auxiliaries')
        yield from settle()
        check(all(not d.showDocked for d in node.docked), 'new helpers collapsed')
        check(cell('decode.auxiliary_preparations') == '0', 'collapsed unused maps have zero float preparations')
        check(cell('outputs.disparity.state') == 'inactive', 'availability distinguished from inactive output')
        disparity = helper('disparity')
        color = node.numpyArray(delayed=False).copy()
        read_ms = cell('decode.read_ms')
        disparity.showDocked = True
        yield from wait(lambda: cell('outputs.disparity.active') == 'true', 'expanding resumes while paused')
        check(cell('decode.auxiliary_preparations') == '1', 'only expanded map prepared')
        check(cell('decode.read_ms') == read_ms, 'expanding does not reread source')
        check(float(abs(node.numpyArray(delayed=False)-color).max()) == 0, 'color unchanged by auxiliary preparation')
        values = disparity.numpyArray(delayed=False)
        check(values.shape[:2] == (4, 2) and float(values.max()) > 60, 'resumed disparity has native size and values')
        disparity.showDocked = False
        yield from wait(lambda: cell('outputs.disparity.state') == 'inactive', 'collapsed disconnected helper suspends')
        count = disparity.totalCooks
        yield from settle()
        check(disparity.totalCooks == count, 'idle polling does not force helper cooks')
        sink = group.create(nullTOP, 'wired')
        sink.inputConnectors[0].connect(disparity)
        yield from wait(lambda: cell('outputs.disparity.active') == 'true', 'connecting collapsed helper resumes while paused')
        check(cell('decode.auxiliary_preparations') == '1', 'reconnecting reuses prepared cache')
        check(float(sink.numpyArray(delayed=False).max()) > 60, 'connected sink receives current disparity')
        sink.inputConnectors[0].disconnect()
        yield from wait(lambda: cell('outputs.disparity.state') == 'inactive', 'disconnect suspends again')
        reference = group.create(selectTOP, 'unwired_reference')
        reference.par.top = disparity
        yield from wait(lambda: cell('outputs.disparity.active') == 'true', 'Select TOP reference counts as use without a wire')
        check(float(reference.numpyArray(delayed=False).max()) > 60, 'unwired reference receives current pixels')
        node.par.File = str(fixtures/'color.png')
        yield from wait(lambda: cell('file.name') == 'color.png', 'paused source change completes')
        yield from settle(.3)
        check(helper('disparity') is disparity and 'Unavailable' in disparity.comment, 'referenced unavailable helper is preserved and annotated')
        check(float(abs(reference.numpyArray(delayed=False)[..., :3]).max()) == 0, 'unavailable referenced output clears stale pixels')
        reference.par.top = ''
        node.par.File = str(fixtures/'portrait.heic')
        yield from wait(lambda: cell('file.name') == 'portrait.heic', 'paused source restored')
        yield from settle()
        node.par.Refresh.pulse()
        old_read = cell('decode.read_ms')
        yield from wait(lambda: cell('decode.read_ms') != old_read, 'Refresh completes while paused')
        check(cell('decode.auxiliary_preparations') == '0', 'refresh skips unused auxiliary preparation')
        portrait = helper('portrait')
        portrait.showDocked = True
        yield from wait(lambda: cell('outputs.portrait.active') == 'true', 'different helper resumes after refresh')
        check(cell('decode.auxiliary_preparations') == '1', 'refresh prepares only newly requested helper')
        portrait.showDocked = False
        yield from wait(lambda: cell('outputs.portrait.state') == 'inactive', 'second helper suspends')
        direct = group.create(renderselectTOP, 'direct_buffer')
        direct.par.top = node
        direct.par.colorbufindex = 2
        yield from wait(lambda: cell('outputs.disparity.active') == 'true', 'direct Render Select buffer reference resumes')
        check(float(direct.numpyArray(delayed=False).max()) > 60, 'direct buffer receives native disparity')
        direct.par.top = ''
        node.par.Auxiliaryoutputs = 'all'
        yield from wait(lambda: all(cell('outputs.'+kind+'.active') == 'true' for kind in ('disparity', 'portrait', 'skin', 'hair', 'teeth', 'glasses', 'sky')), 'All Available override prepares every available map')
        node.par.Auxiliaryoutputs = 'automatic'
        yield from wait(lambda: cell('outputs.disparity.state') == 'inactive', 'automatic mode restores suspension')
        check(not timeline.play, 'all demand transitions completed with timeline paused')
        # Repeat core transitions with playback running.
        timeline.play = True
        disparity.showDocked = True
        yield from wait(lambda: cell('outputs.disparity.active') == 'true', 'resume during playback')
        disparity.showDocked = False
        yield from wait(lambda: cell('outputs.disparity.state') == 'inactive', 'suspend during playback')
        report['status'] = 'passed'
        report_path.write_text(json.dumps(report, indent=2))
        timeline.play = was_playing
        print('Apple Image In demand PASS:', len(report['checks']), 'checks')

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
            report_path.write_text(json.dumps(report, indent=2))
            print(report['error'])
            return
        run('args[0]()', advance, delayMilliSeconds=16, delayRef=op.TDResources)
    advance()
    return group
