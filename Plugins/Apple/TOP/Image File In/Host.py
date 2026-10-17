"""Host-thread path resolution and deferred network setup. Embedded at build time.
Adapted from MetaData In resolver and Face Detection docking, layout v2.
"""
import td
import time

_MARKER = 'appleImageFileInOwnerV1'
_ATTEMPTED = 'appleImageFileInAttemptedV1'
_FILE = 'appleImageFileInFileV1'
_KINDS = ('color', 'depth', 'disparity', 'portrait', 'skin', 'hair',
          'teeth', 'glasses', 'sky', 'hdr_gain', 'iso_gain')
_poll_run = None
_parameters = None
_references = set()
_reference_time = -1.0
_demand = 0


def _refresh_references(owner, now, force=False):
    global _references, _reference_time
    if not force and now - _reference_time < .25:
        return
    _reference_time = now
    references = set()
    # outputs lists wires only. Native Select/Render Select references also
    # count as use, even when the referring operator is not currently cooking.
    root = td.op('/')
    for kind in (td.selectTOP, td.renderselectTOP):
        for node in root.findChildren(type=kind):
            try:
                target = node.par.top.eval()
            except Exception:
                continue  # An invalid reference has no resolved consumer.
            owned_kind = node.fetch(_MARKER, None, search=False)
            if getattr(target, 'id', None) is not None and not (owned_kind in _KINDS[1:] and _owned(node, owner, owned_kind)):
                if target == owner and kind == td.renderselectTOP:
                    references.add(('buffer', int(node.par.colorbufindex.eval())))
                else:
                    references.add(('node', target.id))
    _references = references


def consumed(node):
    return bool(node.outputs) or ('node', node.id) in _references


def demand(owner):
    global _demand
    now = time.monotonic()
    _refresh_references(owner, now)
    mask = 0
    for node in owner.docked:
        kind = node.fetch(_MARKER, None, search=False)
        if kind not in _KINDS[1:] or not _owned(node, owner, kind):
            continue
        slot = _KINDS.index(kind)
        # Initial/setup cooks are not evidence of a consumer. Treat only
        # visible helpers, wires and resolved native references as automatic use.
        if node.showDocked or consumed(node):
            mask |= 1 << slot
    for kind, slot in _references:
        if kind == 'buffer' and 0 < slot < len(_KINDS):
            mask |= 1 << slot
    _demand = mask
    return mask


def resolve(node_id):
    owner = td.op(node_id)
    if owner is None or not owner.valid:
        raise ValueError('Image File In node no longer exists')
    value = owner.par.File.eval()
    if not isinstance(value, str) or not value.strip():
        return ''
    if '\x00' in value or '://' in value or value.lower().startswith(('vfs:', 'file:', 'http:', 'https:')):
        raise ValueError('Select one local filesystem file; URLs and VFS paths are unsupported')
    return str(owner.par.File.evalFile().absPath)


def _owned(node, owner, kind):
    return (node is not None and node.valid and node.dock == owner
            and node.fetch(_MARKER, None, search=False) == kind
            and node.type == ('info' if kind == 'metadata' else 'renderselect'))


def _place(node, owner, top, collapse):
    # Show Docked belongs to each child, not its dock parent. Position its
    # expanded viewer, then hide it so expanding later reveals a vertical stack.
    shown = False if collapse else node.showDocked
    node.showDocked = True
    node.viewer = True
    node.nodeX = owner.nodeX
    bottom = top - node.nodeHeight
    node.nodeY = bottom
    node.showDocked = shown
    return bottom - 30


def reconcile(owner, state, repair=False, initial=False):
    path, status, mask, fallback = state
    if not owner.fetch("appleImageFileInDefaultsV1", False, search=False):
        if str(owner.par.format.mode).lower().split(".")[-1] == "constant" and owner.par.format.eval() == "rgba8fixed":
            owner.par.format = "rgba16float"
        owner.store("appleImageFileInDefaultsV1", True)
    migrate_layout = not owner.fetch('appleImageFileInColumnV1', False, search=False)
    previous = owner.fetch(_FILE, None, search=False)
    attempted = set(owner.fetch(_ATTEMPTED, (), search=False))
    if repair or initial or previous != path:
        attempted.clear()
    warnings = []
    layout_nodes, created_nodes = [], []
    found = {kind: next((node for node in owner.docked if _owned(node, owner, kind)), None)
             for kind in ('metadata',) + _KINDS[1:]}
    metadata = found['metadata']
    if metadata is None and 'metadata' not in attempted:
        metadata = owner.parent().create(td.infoDAT, owner.name + '_metadata')
        metadata.dock = owner
        metadata.store(_MARKER, 'metadata')
        created_nodes.append(metadata)
        attempted.add('metadata')
    if metadata is not None:
        attempted.add('metadata')
        metadata.par.op.expr = 'me.dock'
        metadata.par.passive = False
        layout_nodes.append(metadata)
    # Keep helpers in place during asynchronous replacement; their old buffers
    # are already cleared by the TOP on an explicit source/selection change.
    if status != 'reading':
        if any(found[kind] is not None and not (mask & (1 << slot)) for slot, kind in enumerate(_KINDS[1:], 1)):
            _refresh_references(owner, time.monotonic(), force=True)
        for slot, kind in enumerate(_KINDS[1:], 1):
            node = found[kind]
            available = bool(mask & (1 << slot))
            created = False
            if available and node is None and kind not in attempted:
                node = owner.parent().create(td.renderselectTOP, owner.name + '_' + kind)
                node.dock = owner
                node.store(_MARKER, kind)
                node.store('appleImageOriginalColor', node.color)
                created = True
                created_nodes.append(node)
            if node is not None:
                if available:
                    node.par.top.expr = 'me.dock'
                    node.par.colorbufindex = slot
                    node.par.format = 'useinput'
                    node.comment = 'Apple Image In: ' + kind
                    node.color = node.fetch('appleImageOriginalColor', node.color, search=False)
                    attempted.add(kind)
                    layout_nodes.append(node)
                elif consumed(node):
                    node.par.top.expr = 'me.dock'
                    node.par.colorbufindex = slot
                    node.comment = 'Unavailable: current image has no usable ' + kind + ' data. Output cleared.'
                    node.color = (0.35, 0.35, 0.35)
                    warnings.append(kind + ' is unavailable; its used selector was preserved')
                    layout_nodes.append(node)
                else:
                    node.destroy()
                    # A helper removed because data disappeared may return later.
                    attempted.discard(kind)
            # If a user deleted an available helper, attempted keeps it suppressed.
    else:
        layout_nodes.extend(found[kind] for kind in _KINDS[1:] if found[kind] is not None)
    if migrate_layout or repair or initial or previous != path or created_nodes:
        top = owner.nodeY - 30
        for node in layout_nodes:
            top = _place(node, owner, top, migrate_layout or node in created_nodes)
        owner.store('appleImageFileInColumnV1', True)
    if fallback and str(owner.par.Image.mode).lower().split(".")[-1] == "constant":
        owner.par.Image = 'primary'
    owner.store(_FILE, path)
    owner.store(_ATTEMPTED, tuple(sorted(attempted)))
    return '\n'.join(warnings)


def dispatch(node_id, repair=False, initial=False):
    owner = td.op(node_id)
    if owner is None or not owner.valid:
        return
    report = getattr(owner, '_imageDockResult', None)
    state = getattr(owner, '_imageDockState', None)
    if report is None or state is None:
        return
    try:
        warning = reconcile(owner, state(), repair, initial)
    except Exception as error:
        report('Could not create/repair docked outputs: ' + str(error))
    else:
        report(warning)


def schedule(node_id, repair=False, initial=False):
    # Evaluate state at execution time; a queued callback never holds C++ pointers.
    td.run("args[0](args[1], args[2], args[3])", dispatch, node_id, repair, initial,
           delayMilliSeconds=1, delayRef=td.op.TDResources)
    ensure_poll(node_id)


def ensure_poll(node_id):
    global _poll_run
    if _poll_run is None:
        _poll_run = td.run('args[0](args[1])', poll, node_id,
                          delayMilliSeconds=16, delayRef=td.op.TDResources)


def poll(node_id):
    global _poll_run, _parameters
    _poll_run = None
    owner = td.op(node_id)
    if owner is None or not owner.valid:
        return
    pending = getattr(owner, '_imageNeedsPublish', None)
    if pending is None:
        return
    try:
        # Covers Common parameters and expressions as well as File/Image/Decode.
        # A changed value causes one cook; unchanged images are not re-uploaded.
        values = tuple((p.name, repr(p.eval())) for p in owner.pars())
        changed = values != _parameters
        _parameters = values
        setter = getattr(owner, '_imageSetDemand', None)
        demand_changed = setter(demand(owner)) if setter is not None else False
        if changed or demand_changed or pending():
            owner.cook(force=True)
    except Exception as error:
        report = getattr(owner, '_imageDockResult', None)
        if report is not None:
            report('Could not update image while paused: ' + str(error))
    ensure_poll(node_id)


def shutdown():
    global _poll_run, _references
    if _poll_run is not None:
        try:
            _poll_run.kill()
        except Exception:
            pass  # Already executed or invalidated during project shutdown.
        _poll_run = None
    _references = set()
