"""Exercise generic resolution without requiring a running TouchDesigner host."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location("resolver", Path(__file__).resolve().parents[1] / "Resolver.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Parameter:
    def __init__(self, name, value, label=None, resolved="/resolved/source.bin"):
        self.name, self.label = name, label or name
        self.value, self.resolved = value, resolved

    def eval(self):
        return self.value() if callable(self.value) else self.value

    def evalFile(self):
        return SimpleNamespace(absPath=self.resolved)


class Operator:
    valid = True

    def __init__(self, *parameters):
        self.parameters = parameters
        self.par = SimpleNamespace(**{p.name: p for p in parameters})

    def pars(self):
        return self.parameters


class Tests(unittest.TestCase):
    def node(self, target):
        return Operator(Parameter("File", "direct", resolved="/direct.bin"), Parameter("Operator", target))

    def test_direct_and_evaluated(self):
        target = Operator(Parameter("file", lambda: "project:/dynamic.bin"))
        node = self.node(target)
        self.assertEqual(module.resolve(node, True, False), "/direct.bin")
        self.assertEqual(module.resolve(node, False, True), "/resolved/source.bin")

    def test_empty_and_conflicting_sources(self):
        node = self.node(Operator(Parameter("File", "a")))
        self.assertEqual(module.resolve(node, False, False), "")
        with self.assertRaisesRegex(ValueError, "both set"):
            module.resolve(node, True, True)

    def test_generic_names_labels_and_name_precedence(self):
        for name in ["file", "File", "FILE", "FiLe"]:
            self.assertEqual(module.resolve(self.node(Operator(Parameter(name,"a"))), False, True), "/resolved/source.bin")
        self.assertEqual(module.resolve(self.node(Operator(Parameter("Custompath","a",label="File"))), False, True), "/resolved/source.bin")
        target = Operator(Parameter("file","a",resolved="/named.bin"), Parameter("Otherpath","b",label="File"))
        self.assertEqual(module.resolve(self.node(target), False, True), "/named.bin")

    def test_ambiguity_missing_multiple_deleted_and_self(self):
        targets = [None, Operator(), Operator(Parameter("File","a"), Parameter("file","b")),
                   Operator(Parameter("A","a",label="File"),Parameter("B","b",label="File"))]
        targets += [[Operator(), Operator()]]
        deleted = Operator(Parameter("file","a")); deleted.valid = False; targets.append(deleted)
        for target in targets:
            with self.assertRaises(ValueError): module.resolve(self.node(target), False, True)
        node = self.node(None); node.par.Operator.value = node
        with self.assertRaises(ValueError): module.resolve(node, False, True)

    def test_rename_reference_and_forbidden_paths(self):
        target = Operator(Parameter("file","a")); node = self.node(target)
        target.name = "renamed"
        self.assertEqual(module.resolve(node, False, True), "/resolved/source.bin")
        for value in ["https://example.test/a", "vfs:/owner:a", "file:///tmp/a", "", "bad\x00path", ["a","b"]]:
            target.par.file.value = value
            with self.assertRaises(ValueError): module.resolve(node, False, True)


if __name__ == "__main__":
    unittest.main()
