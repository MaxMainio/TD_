"""Embedded into the plugin at build time; executed by td.run at frame end.

Only network setup lives here. Hull table values come from the C++ Info DAT API.
"""
import td

# Retain the prototype storage key so existing/retyped DATs keep their layout state.
_MARKER = "convexHullDockedInfoDATVersion"
_LAYOUT_VERSION = 2


def _matches(dat, owner):
    if dat is None or not dat.valid or dat.type != "info":
        return False
    # Check ownership before evaluating a possibly broken user-edited expression.
    if dat.dock == owner and dat.fetch(_MARKER, 0, search=False) in (1, _LAYOUT_VERSION):
        return True
    try:
        return dat.par.op.eval() == owner
    except Exception:
        return False


def ensure_hull_dat(owner, reset_layout=False):
    """Reuse this owner's table, or let TouchDesigner choose a free new name."""
    expected_name = owner.name + "_hulls"
    existing = next((dat for dat in owner.docked if _matches(dat, owner)), None)
    if existing is None:
        candidate = owner.parent().op(expected_name)
        if _matches(candidate, owner) and candidate.dock in (None, owner):
            existing = candidate
    created = existing is None
    dat = owner.parent().create(td.infoDAT, expected_name) if created else existing
    layout_version = dat.fetch(_MARKER, 0, search=False)
    # The expression follows the dock parent through rename, copy, and save/load.
    dat.dock = owner
    # Mark ownership before changing references, so a partially repaired DAT can
    # be found on retry. Upgrade the marker only after layout succeeds.
    dat.store(_MARKER, layout_version if layout_version in (1, _LAYOUT_VERSION) else 1)
    dat.par.op.expr = "me.dock"
    dat.par.passive = False
    if created or reset_layout or layout_version != _LAYOUT_VERSION:
        shown = dat.showDocked if not created else False
        # Size/position the expanded viewer before hiding the dock. nodeY is the
        # bottom edge, so subtract the DAT's height to put its top below the TOP.
        dat.showDocked = True
        dat.viewer = True
        dat.nodeX = owner.nodeX
        dat.nodeY = owner.nodeY - dat.nodeHeight - 30
        dat.showDocked = shown
    dat.store(_MARKER, _LAYOUT_VERSION)
    return dat


def dispatch(node_id, reset_layout=False):
    owner = td.op(node_id)
    # Never hold a C++ instance pointer across the deferred callback.
    if owner is None or not owner.valid:
        return
    report = getattr(owner, "_hullDockResult", None)
    if report is None:  # Python integration requires an installed Custom OP.
        return
    try:
        ensure_hull_dat(owner, reset_layout)
    except Exception as error:
        report("Could not create/repair Hull Info DAT: " + str(error))
    else:
        report("")


if "args" in globals():
    dispatch(args[0], args[1] if len(args) > 1 else False)
