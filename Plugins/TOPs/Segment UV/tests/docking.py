"""Network behavior tests with a small simulated TouchDesigner interface."""
from pathlib import Path
import sys
import types
import unittest


class Parameter:
    def __init__(self, owner, value=None):
        self.owner, self.val, self.expr = owner, value, ""

    def eval(self):
        if self.expr == "broken expression":
            raise RuntimeError("invalid reference expression")
        return self.owner.dock if self.expr == "me.dock" else self.val


class Parameters:
    def __init__(self, owner):
        self.op = Parameter(owner)
        self.passive = Parameter(owner, True)

    def __setattr__(self, name, value):
        existing = self.__dict__.get(name)
        if isinstance(existing, Parameter):
            existing.val = value
        else:
            super().__setattr__(name, value)


class Node:
    def __init__(self, network, name, kind):
        self.network, self.name, self.type = network, name, kind
        self.id = network.next_id
        network.next_id += 1
        self.dock, self.valid, self.showDocked = None, True, True
        self.viewer, self.nodeX, self.nodeY = False, 0, 0
        self.par, self.storage, self.reports = Parameters(self), {}, []
        network.nodes.append(self)

    @property
    def nodeHeight(self):
        return 90 if self.viewer else 30

    @property
    def docked(self):
        return [node for node in self.network.nodes if node.valid and node.dock is self]

    def parent(self):
        return self.network

    def store(self, key, value):
        self.storage[key] = value

    def fetch(self, key, default=None, search=True):
        return self.storage.get(key, default)

    def _segmentDockResult(self, message):
        self.reports.append(message)


class Network:
    def __init__(self):
        self.nodes, self.next_id, self.fail_create = [], 1, False

    def op(self, name):
        return next((node for node in self.nodes if node.valid and node.name == name), None)

    def by_id(self, identity):
        return next((node for node in self.nodes if node.valid and node.id == identity), None)

    def create(self, kind, name):
        if self.fail_create:
            raise RuntimeError("network is locked")
        candidate, index = name, 1
        while self.op(candidate):
            candidate = name + str(index)
            index += 1
        return Node(self, candidate, kind)


class DockingTests(unittest.TestCase):
    def setUp(self):
        self.network = Network()
        self.td = types.SimpleNamespace(infoDAT="info", op=self.network.by_id)
        sys.modules["td"] = self.td
        self.namespace = {}
        self.source = (Path(__file__).resolve().parents[1] / "Docking.py").read_text()
        exec(compile(self.source, "Docking.py", "exec"), self.namespace)
        self.owner = self.network.create("segmentuv", "segmentuv1")
        self.owner.nodeX, self.owner.nodeY = 370, -240

    def dispatch(self, owner=None, reset_layout=False):
        self.namespace["dispatch"]((owner or self.owner).id, reset_layout)

    def test_create_repeated_setup_and_repair(self):
        self.dispatch()
        dat = self.owner.docked[0]
        self.assertEqual(dat.name, "segmentuv1_segments")
        self.assertEqual(dat.par.op.eval(), self.owner)
        self.assertFalse(dat.par.passive.eval())
        self.assertFalse(dat.showDocked)
        self.assertTrue(dat.viewer)
        self.assertEqual(dat.nodeX, self.owner.nodeX)
        self.assertEqual(dat.nodeY + dat.nodeHeight, self.owner.nodeY - 30)
        for _ in range(4):
            self.dispatch()
        self.assertEqual(self.owner.docked, [dat])
        self.assertEqual(self.owner.reports[-1], "")
        dat.valid = False
        # There is no recurring timer: deletion persists until explicitly dispatched.
        self.assertEqual(self.owner.docked, [])
        self.dispatch()
        self.assertEqual(len(self.owner.docked), 1)
        self.assertIsNot(self.owner.docked[0], dat)

    def test_rename_and_saved_table_reuse(self):
        self.dispatch()
        dat = self.owner.docked[0]
        dat.showDocked = True
        dat.nodeX, dat.nodeY, dat.viewer = 540, -500, False
        self.owner.name = "renamed"
        self.dispatch()
        self.assertEqual(self.owner.docked, [dat])
        self.assertIs(dat.par.op.eval(), self.owner)
        self.assertTrue(dat.showDocked)
        self.assertEqual((dat.nodeX, dat.nodeY, dat.viewer), (540, -500, False))
        # A module reload represents a new plugin instance using saved node state.
        exec(compile(self.source, "Docking.py", "exec"), self.namespace)
        self.dispatch()
        self.assertEqual(self.owner.docked, [dat])

    def test_unfinished_layout_retries_once_and_explicit_repair_resets_layout(self):
        dat = self.network.create("info", self.owner.name + "_segments")
        dat.dock, dat.par.op.expr = self.owner, "me.dock"
        dat.store("segmentUVInfoDATVersion", 0)
        dat.showDocked = False
        self.dispatch()
        self.assertTrue(dat.viewer)
        self.assertFalse(dat.showDocked)
        self.assertEqual(dat.nodeX, self.owner.nodeX)
        self.assertEqual(dat.nodeY + dat.nodeHeight, self.owner.nodeY - 30)
        dat.nodeX, dat.nodeY, dat.viewer = 100, 200, False
        self.dispatch()
        self.assertEqual((dat.nodeX, dat.nodeY, dat.viewer), (100, 200, False))
        self.dispatch(reset_layout=True)
        self.assertTrue(dat.viewer)
        self.assertEqual(dat.nodeX, self.owner.nodeX)
        self.assertEqual(dat.nodeY + dat.nodeHeight, self.owner.nodeY - 30)

    def test_duplicate_with_and_without_table(self):
        self.dispatch()
        duplicate = self.network.create("segmentuv", "copy")
        copied = self.network.create("info", "copy_segments")
        copied.dock, copied.par.op.expr = duplicate, "me.dock"
        copied.storage = dict(self.owner.docked[0].storage)
        self.dispatch(duplicate)
        self.assertEqual(duplicate.docked, [copied])
        self.assertIs(copied.par.op.eval(), duplicate)
        bare_copy = self.network.create("segmentuv", "bare_copy")
        self.dispatch(bare_copy)
        self.assertEqual(len(bare_copy.docked), 1)
        self.assertIs(bare_copy.docked[0].par.op.eval(), bare_copy)

    def test_name_collision_and_unrelated_docked_dat(self):
        collision = self.network.create("table", self.owner.name + "_segments")
        other_owner = self.network.create("segmentuv", "other")
        unrelated = self.network.create("info", "unrelated")
        unrelated.par.op.val, unrelated.dock = other_owner, self.owner
        self.dispatch()
        self.assertEqual(collision.type, "table")
        self.assertIs(unrelated.par.op.eval(), other_owner)
        self.assertEqual(len(self.owner.docked), 2)
        created = next(node for node in self.owner.docked if node is not unrelated)
        self.assertNotEqual(created.name, collision.name)
        self.dispatch()
        self.assertEqual(len(self.owner.docked), 2)

    def test_reuse_existing_matching_info_and_repair_reference(self):
        dat = self.network.create("info", self.owner.name + "_segments")
        dat.par.op.val = self.owner
        self.dispatch()
        self.assertEqual(self.owner.docked, [dat])
        dat.par.op.expr, dat.par.op.val = "broken expression", None
        self.dispatch()
        self.assertIs(dat.par.op.eval(), self.owner)

    def test_unrelated_invalid_expression_is_preserved(self):
        dat = self.network.create("info", self.owner.name + "_segments")
        dat.dock, dat.par.op.expr = self.owner, "broken expression"
        self.dispatch()
        self.assertEqual(dat.par.op.expr, "broken expression")
        self.assertEqual(len(self.owner.docked), 2)
        self.assertEqual(self.owner.reports[-1], "")

    def test_deleted_owner_and_failure_recovery(self):
        self.network.fail_create = True
        self.dispatch()
        self.assertIn("network is locked", self.owner.reports[-1])
        self.network.fail_create = False
        self.dispatch()
        self.assertEqual(self.owner.reports[-1], "")
        self.owner.valid = False
        self.dispatch()
        self.assertEqual(len(self.network.nodes), 2)

    def test_embedded_run_entrypoint(self):
        exec(compile(self.source, "Docking.py", "exec"), {"args": (self.owner.id,)})
        self.assertEqual(len(self.owner.docked), 1)


if __name__ == "__main__":
    unittest.main()
