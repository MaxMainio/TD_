import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

class Par:
    def __init__(self, value="sample"):
        self.value = value
        self.expr = None
        self.mode = 'constant'
    def eval(self):
        return self.value
    def evalFile(self):
        return SimpleNamespace(absPath='/project/sample')
class Pars:
    def __init__(self):
        for k in ('op', 'passive', 'top', 'colorbufindex', 'format', 'Image', 'File'):
            setattr(self, k, Par())
class Node:
    def __init__(self, parent, kind, name):
        self._parent, self.type, self.name = parent, kind, name
        self.id, self.totalCooks = id(self), 0
        self.valid, self.dock, self.showDocked = True, None, False
        self.nodeX, self.nodeY, self.nodeHeight = 0, 0, 100
        self.color, self.comment, self.outputs = (1, 1, 1), '', []
        self.par, self.storage = Pars(), {}
    def parent(self): return self._parent
    @property
    def docked(self): return [x for x in self._parent.nodes if x.dock is self and x.valid]
    def fetch(self, key, default=None, search=False): return self.storage.get(key, default)
    def store(self, key, value): self.storage[key] = value
    def destroy(self): self.valid = False
class Network:
    def __init__(self): self.nodes = []
    def findChildren(self, type):return [n for n in self.nodes if n.valid and n.type == type]
    def create(self, kind, name):
        n = Node(self, kind, name); self.nodes.append(n); return n
network = Network()
sys.modules['td'] = SimpleNamespace(infoDAT='info', renderselectTOP='renderselect', selectTOP='select', ParMode=SimpleNamespace(CONSTANT='constant'))
spec = importlib.util.spec_from_file_location('image_host', Path(__file__).parents[1] / 'Host.py')
host = importlib.util.module_from_spec(spec); spec.loader.exec_module(host)
class Tests(unittest.TestCase):
    def setUp(self):
        self.net = Network(); self.owner = self.net.create('appleimagefilein', 'image1')
        host._references=set();host._reference_time=-1;host._demand=0
        class Lookup:
            TDResources=object()
            def __call__(lookup, value):return self.net if value=='/' else self.owner
        host.td.op=Lookup()
    def state(self, mask=1, file='a', status='ready'):
        return (file, status, mask, False)
    def node(self, kind):
        return next((n for n in self.owner.docked if n.fetch(host._MARKER) == kind), None)
    def test_lifecycle(self):
        other=self.net.create('info','image1_metadata')
        host.reconcile(self.owner,self.state(1|4|8))
        self.assertIsNot(self.node('metadata'),other)
        depth=self.node('disparity'); matte=self.node('portrait')
        self.assertEqual(depth.par.top.expr,'me.dock'); self.assertEqual(depth.par.colorbufindex,2)
        depth.outputs=[object()]
        warning=host.reconcile(self.owner,self.state(file='b'))
        self.assertTrue(depth.valid); self.assertFalse(matte.valid)
        self.assertIn('unavailable',warning); self.assertIn('Unavailable',depth.comment)
        host.reconcile(self.owner,self.state(1|4,file='c'))
        self.assertIs(self.node('disparity'),depth); self.assertNotIn('Unavailable',depth.comment)
    def test_user_deletion(self):
        host.reconcile(self.owner,self.state(1|4)); self.node('disparity').destroy()
        host.reconcile(self.owner,self.state(1|4));self.assertIsNone(self.node('disparity'))
        host.reconcile(self.owner,self.state(1|4),repair=True);self.assertIsNotNone(self.node('disparity'))
        self.node('metadata').destroy();host.reconcile(self.owner,self.state(1|4));self.assertIsNone(self.node('metadata'))
        host.reconcile(self.owner,self.state(1|4),initial=True);self.assertIsNotNone(self.node('metadata'))
    def test_reading_and_new_file(self):
        host.reconcile(self.owner,self.state(1|4));depth=self.node('disparity')
        host.reconcile(self.owner,self.state(0,file='b',status='reading'));self.assertTrue(depth.valid)
        host.reconcile(self.owner,self.state(1,file='b'));self.assertFalse(depth.valid)
    def test_no_missing_outputs(self):
        host.reconcile(self.owner,self.state());self.assertEqual(len(self.owner.docked),1)
    def test_rename(self):
        host.reconcile(self.owner,self.state(1|4));d=self.node('disparity');self.owner.name='renamed'
        host.reconcile(self.owner,self.state(1|4));self.assertIs(self.node('disparity'),d);self.assertEqual(d.par.top.expr,'me.dock')
    def test_collapsed_default_preserves_user_expansion(self):
        host.reconcile(self.owner,self.state(1|4))
        self.assertTrue(all(not n.showDocked for n in self.owner.docked))
        metadata=self.node('metadata');depth=self.node('disparity')
        self.assertEqual(metadata.nodeX,self.owner.nodeX)
        self.assertEqual(depth.nodeX,self.owner.nodeX)
        self.assertLess(depth.nodeY,metadata.nodeY-depth.nodeHeight)
        depth.showDocked=True
        host.reconcile(self.owner,self.state(1|4),repair=True)
        self.assertTrue(depth.showDocked)
        self.assertFalse(metadata.showDocked)
    def test_independent_clock_and_coalesced_polling(self):
        calls=[]
        class Lookup:
            TDResources=object()
            def __call__(lookup, node_id):return self.owner if self.owner.valid else None
        class Run:
            killed=False
            def kill(run):run.killed=True
        def run(*args,**kwargs):
            calls.append((args,kwargs));return Run()
        host.td.op=Lookup();host.td.run=run
        self.owner._imageNeedsPublish=lambda:False
        self.owner.pars=lambda:[]
        cooks=[];self.owner.cook=lambda **kwargs:cooks.append(kwargs)
        host._parameters=None;host._poll_run=None
        host.schedule(1);host.ensure_poll(1)
        self.assertEqual(len(calls),2)
        self.assertTrue(all(c[1]['delayRef'] is host.td.op.TDResources for c in calls))
        host.poll(1);self.assertEqual(len(cooks),1)
        host.poll(1);self.assertEqual(len(cooks),1)
        self.owner._imageNeedsPublish=lambda:True
        host.poll(1);self.assertEqual(len(cooks),2)
        host.shutdown();self.assertIsNone(host._poll_run)
        self.owner.valid=False;count=len(calls);host.poll(1);self.assertEqual(len(calls),count)
    def test_demand_visibility_connections_references_and_requests(self):
        host.reconcile(self.owner,self.state(1|4|8))
        # No external selectors initially; owned selectors are excluded.
        for n in self.owner.docked:
            if n.type=='renderselect':n.par.top.value=self.owner
        self.assertEqual(host.demand(self.owner),0)
        depth=self.node('disparity');matte=self.node('portrait')
        depth.showDocked=True;self.assertEqual(host.demand(self.owner),4)
        depth.showDocked=False;depth.outputs=[object()];self.assertEqual(host.demand(self.owner),4)
        depth.outputs=[];self.assertEqual(host.demand(self.owner),0)
        ref=self.net.create('select','reference');ref.par.top.value=matte
        host._reference_time=-1;self.assertEqual(host.demand(self.owner),8)
        ref.destroy();host._reference_time=-1;self.assertEqual(host.demand(self.owner),0)
        depth.totalCooks+=1;self.assertEqual(host.demand(self.owner),0)
        direct=self.net.create('renderselect','direct');direct.par.top.value=self.owner;direct.par.colorbufindex.value=3
        host._reference_time=-1;self.assertEqual(host.demand(self.owner),8)
    def test_referenced_unavailable_selector_is_preserved(self):
        host.reconcile(self.owner,self.state(1|4));depth=self.node('disparity')
        ref=self.net.create('select','reference');ref.par.top.value=depth
        self.assertIn('unavailable',host.reconcile(self.owner,self.state(file='new')))
        self.assertTrue(depth.valid)
unittest.main()
