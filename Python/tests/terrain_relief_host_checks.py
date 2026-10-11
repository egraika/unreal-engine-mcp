"""Offline relief orchestration checks: no Unreal imports or live editor calls."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from helpers import prk_terrain_author_host as H


class ReliefWorld:
    def __init__(self, *, active=False, fail=None, busy=2):
        self.active = active
        self.fail = fail
        self.busy = busy
        self.calls = []
        self.t = 0.0
        self.sleeps = []

    def sleep(self, seconds):
        self.t += seconds
        self.sleeps.append(seconds)

    def clock(self):
        return self.t

    def dispatch(self, module, func, **kwargs):
        self.calls.append((module, func, kwargs, self.active))
        if func == "stamp_source":
            return {"ok": True, "actor": "CanonicalHill", "texture": None, "edit_mode_active": self.active}
        if func == "stamp_relief":
            if self.fail == "plan" or (self.active and kwargs.get("crest_above_ground_m") is not None):
                return {"ok": False, "refused": True, "error": "Cannot measure stale terrain."}
            return {"ok": True, "dry_run": kwargs.get("dry_run", True),
                    "configure_args": {"scale": 0.15, "invert": False, "location_z": 900.0},
                    "measurement": {"ring_median_cm": 2000.0, "heights_stale": self.active},
                    "configure": {"ok": True, "dry_run": kwargs.get("dry_run", True)}}
        if func == "edit_mode_set":
            assert kwargs["preserve_camera"] is True
            assert kwargs["dry_run"] is False
            if not kwargs["active"] and self.fail == "exit_exception":
                raise RuntimeError("Uncertain mode exit response")
            if self.fail == ("enter" if kwargs["active"] else "exit"):
                return {"ok": False, "refused": True, "error": "Mode guard refused."}
            self.active = kwargs["active"]
            return {"ok": True}
        if func == "stamp_configure":
            assert module == "prk_lake_ops" and self.active
            assert kwargs["dry_run"] is False
            assert kwargs["actor"] == "CanonicalHill"
            assert kwargs["scale"] == 0.15 and kwargs["location_z"] == 900.0
            assert kwargs["max_dirty_proxies"] == 8
            if self.fail == "configure_exception":
                raise RuntimeError("Uncertain transport response")
            if self.fail == "configure":
                return {"ok": False, "refused": True, "error": "Configure guard refused."}
            return {"ok": True, "verified": True}
        if func == "view_ready":
            if self.fail == "ready_exception":
                raise RuntimeError("Readiness response unavailable")
            self.busy -= 1
            return {"ok": True, "pie": False, "subsystem_idle": self.busy < 0}
        raise AssertionError(func)

    def run(self, **kwargs):
        return H.stamp_relief(dispatch=self.dispatch, actor="Alias", k_peak=0.9,
                              expect_world="world", max_dirty_proxies=8,
                              clock=self.clock, sleep=self.sleep, **kwargs)

    def functions(self):
        return [c[1] for c in self.calls]


w = ReliefWorld()
r = w.run(crest_above_ground_m=10, dry_run=False)
assert r["ok"] and r["applied"] and r["committed"] and not w.active
assert w.functions() == ["stamp_source", "stamp_relief", "edit_mode_set", "stamp_configure",
                         "view_ready", "view_ready", "view_ready", "edit_mode_set"]
assert w.calls[1][2]["dry_run"] is True and w.calls[1][3] is False
assert w.sleeps == [0.5, 0.5]
print("PASS terrain target plans committed heights once, writes in guarded mode, ticks and exits")

w = ReliefWorld()
r = w.run(relief_factor=1.5, dry_run=False)
assert r["ok"] and r["committed"] and not w.active
print("PASS ordinary relief from committed mode also avoids mode-OFF rendering")

w = ReliefWorld(active=True)
r = w.run(relief_factor=1.5, dry_run=False)
assert r["ok"] and w.active and w.functions() == ["stamp_source", "stamp_relief"]
assert w.calls[1][2]["dry_run"] is False
print("PASS active-mode relief retains batch mode")

w = ReliefWorld(active=True)
r = w.run(crest_above_ground_m=10, dry_run=False)
assert not r["ok"] and r["refused"] and w.active
assert w.functions() == ["stamp_source", "stamp_relief"]
print("PASS active terrain target refuses stale measurement without mode changes")

w = ReliefWorld()
r = w.run(crest_above_ground_m=10, dry_run=True)
assert r["ok"] and r["dry_run"] and not w.active
assert w.functions() == ["stamp_source", "stamp_relief"]
print("PASS dry run never enters or writes")

for failure in ("plan", "enter"):
    w = ReliefWorld(fail=failure)
    r = w.run(crest_above_ground_m=10, dry_run=False)
    assert not r["ok"] and not w.active and "stamp_configure" not in w.functions()
print("PASS plan and entry refusals cannot write")

for failure in ("configure", "configure_exception"):
    w = ReliefWorld(fail=failure, busy=0)
    r = w.run(crest_above_ground_m=10, dry_run=False)
    assert not r["ok"] and not r["applied"] and r["committed"] and not w.active
    assert w.functions().count("stamp_configure") == 1 and w.functions()[-1] == "edit_mode_set"
    if failure == "configure_exception":
        assert r["configure"]["write_uncertain"]
print("PASS failed or uncertain configure exits owned mode without replaying write")

w = ReliefWorld(fail="exit", busy=0)
r = w.run(crest_above_ground_m=10, dry_run=False)
assert not r["ok"] and r["applied"] and not r["committed"] and w.active
print("PASS exit failure reports applied but uncommitted terrain")

for failure in ("ready_exception", "exit_exception"):
    w = ReliefWorld(fail=failure, busy=0)
    r = w.run(crest_above_ground_m=10, dry_run=False)
    assert not r["ok"] and r["applied"] and not r["committed"]
    assert w.functions().count("stamp_configure") == 1
    if failure == "exit_exception":
        assert r["commit_uncertain"]
print("PASS readiness and exit transport failures retain applied-status evidence")

w = ReliefWorld(busy=100)
r = w.run(crest_above_ground_m=10, dry_run=False, timeout_s=1.0)
assert not r["ok"] and r["applied"] and not r["committed"] and w.active
assert w.functions().count("edit_mode_set") == 1 and w.t == 1.0
print("PASS busy timeout leaves owned mode active and reports incomplete commit")
