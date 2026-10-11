"""Host-side orchestration. Every editor operation is a separate RPC/tick boundary."""

from pathlib import Path
import hashlib
import math
import threading
import time

_RUN_LOCK = threading.Lock()
VIEW_IDS = ("entrance", "squad", "overview")
PNG_END = b"\0\0\0\0IEND\xaeB`\x82"


def complete_png(receipt):
    path = Path(receipt["capture_path"])
    try:
        stat = path.stat()
        if not path.is_file() or not 20 <= stat.st_size <= 32 * 1024 * 1024:
            return False
        if stat.st_mtime_ns < receipt["requested_ns"]:
            return False
        with path.open("rb") as stream:
            head = stream.read(8)
            stream.seek(-12, 2)
            return head == b"\x89PNG\r\n\x1a\n" and stream.read() == PNG_END
    except OSError:
        # Unreal briefly holds an exclusive file handle while writing a capture.
        # Leave it pending so the runner waits for completion under the same run.
        return False


def run(*, dispatch, capture, zone_key, batch_id, run_id, thread_id, action="review", dry_run=True,
        visual_note=None, accepted_packages=None, assessment_token=None,
        fits=None, audit_rules=None,
        tick_seconds=1.0, wait_seconds=30.0, sleep=time.sleep, clock=time.monotonic):
    """Return images for review, or save a previously accepted exact manifest. Never auto-accept."""
    key = dict(zone_key=zone_key, batch_id=batch_id, run_id=run_id, thread_id=thread_id)
    if not _RUN_LOCK.acquire(blocking=False):
        return {"ok": False, "error": "Another zone runner is already active in this MCP server"}
    state, camera_changed, restore_error = {}, False, None
    try:
        if action not in {"review", "accept_save", "status", "recover"}:
            raise ValueError("Choose review, accept_save, status or recover")
        if not all(math.isfinite(v) for v in (tick_seconds, wait_seconds)) \
                or not .05 <= tick_seconds <= 5 or not 1 <= wait_seconds <= 60:
            raise ValueError("tick_seconds must be .05..5 and wait_seconds 1..60")

        def stage(name, **extra):
            deadline = clock() + wait_seconds
            while True:
                result = dispatch("prk_zone_authoring", "batch", **key, action=name, dry_run=False, **extra)
                if result.get("ok", result.get("success", False)):
                    return result
                if name in {"verify", "review_ready"} and result.get("phase") == "verification_failed":
                    return result  # Still return the three diagnostic views; acceptance stays blocked.
                pending = result.get("error", "") in {
                    "Brush rendering is pending; retry on a later editor tick",
                    "Texture builds are still running; prepare on a later call"}
                if not pending or clock() >= deadline:
                    raise RuntimeError(str(result))
                sleep(tick_seconds)

        if action == "status":
            return stage("status")
        if dry_run:
            return dispatch("prk_zone_authoring", "batch", **key, action="preview", dry_run=True,
                            fits=fits, audit_rules=audit_rules)
        if action == "accept_save":
            if not visual_note or not isinstance(accepted_packages, list) or not assessment_token:
                raise ValueError("Supply the visual review, exact returned packages and assessment_token")
            state = stage("status")
            if state["phase"] == "verified_awaiting_visual":
                bundle = state.get("review_bundle") or {}
                state = stage("accept", visual_note=visual_note, accepted_packages=accepted_packages,
                              assessment_token=assessment_token,
                              captures={k: v["path"] for k, v in bundle.get("captures", {}).items()})
                sleep(tick_seconds)
            else:
                bundle = state.get("review_bundle") or {}
                if state["phase"] not in {"accepted", "saving", "saved"} \
                        or assessment_token != bundle.get("assessment_token") \
                        or sorted(set(accepted_packages)) != bundle.get("packages"):
                    raise ValueError("This receipt has not accepted the requested assessment/manifest")
            return stage("save")
        if action == "recover":
            state = stage("status")
            if state["phase"] not in {"recovery_applied", "recovery_committed", "recovered", "recovery_saved"}:
                state = stage("recover")
                sleep(tick_seconds)
            if state["phase"] == "recovery_applied":
                state = stage("commit")
                sleep(tick_seconds)
            if state["phase"] == "recovery_committed":
                state = stage("verify")
            return state  # Recovery saves remain a separate explicit stage.

        receipt = dispatch("prk_zone_authoring", "batch", **key, action="status", dry_run=False)
        if receipt.get("ok", receipt.get("success", False)):
            state = receipt
            if fits is not None and [f["evidence_sha256"] for f in fits] != state.get("fit_evidence"):
                raise ValueError("Fit evidence differs from the prepared receipt; use a new run ID")
            if audit_rules is not None and audit_rules != state.get("audit_rule_overrides"):
                raise ValueError("Audit rules differ from the prepared receipt; use a new run ID")
        elif receipt.get("error") == "No journal exists for this batch":
            state = stage("prepare", fits=fits, audit_rules=audit_rules)
        else:
            raise RuntimeError(str(receipt))
        count = state["feature_count"]
        for _ in range(count + 1):
            if state["phase"] not in {"prepared", "applying"}:
                break
            previous = len(state["completed_features"])
            state = stage("apply_next")
            if len(state["completed_features"]) <= previous:
                raise RuntimeError("Placement made no progress; inspect this journal before retrying")
            sleep(tick_seconds)
        if state["phase"] == "applied":
            state = stage("commit")
            sleep(tick_seconds)
        if state["phase"] == "committed":
            state = stage("verify")
            sleep(tick_seconds)
        if state["phase"] not in {"verified_awaiting_visual", "verification_failed", "accepted", "saved"}:
            raise RuntimeError("The batch needs diagnosis: " + str(state))
        if state.get("review_bundle"):
            for view, artifact in state["review_bundle"]["captures"].items():
                receipt = state.get("review_views", {}).get(view)
                if not receipt or not complete_png(receipt) \
                        or hashlib.sha256(Path(artifact["path"]).read_bytes()).hexdigest() != artifact["sha256"]:
                    raise RuntimeError("A recorded review image changed; verify and capture a fresh review: " + view)
            return state
        captures = {}
        for view in VIEW_IDS:
            receipt = state.get("review_views", {}).get(view)
            if receipt and complete_png(receipt):
                camera_changed = True  # Restore a camera left behind by an interrupted capture run.
            else:
                receipt = dispatch("prk_zone_authoring", "review_view", **key, view_id=view, restore=False)
                if not receipt.get("ok", receipt.get("success", False)):
                    raise RuntimeError(str(receipt))
                camera_changed = True
                sleep(tick_seconds)
                result = capture(file_path=receipt["capture_path"], resolution_x=1920, resolution_y=1080,
                                 mask_enabled=False, show_hud=False)
                if not result.get("success", result.get("ok", False)):
                    raise RuntimeError("Viewport capture refused: " + str(result))
                deadline = clock() + wait_seconds
                while not complete_png(receipt):
                    if clock() >= deadline:
                        raise TimeoutError("PNG is pending; resume this run ID after editor ticks: " + view)
                    sleep(tick_seconds)
            captures[view] = receipt["capture_path"]
        state = stage("review_ready", captures=captures)
        return state
    except Exception as error:
        state = {"ok": False, "error": str(error), "phase": state.get("phase"),
                 "journal": state.get("journal"), "resume_same_run_id": True}
        return state
    finally:
        if camera_changed:
            try:
                restored = dispatch("prk_zone_authoring", "review_view", **key, restore=True)
                if not restored.get("ok", restored.get("success", False)):
                    restore_error = restored
            except Exception as error:
                restore_error = str(error)
        if restore_error:
            state["camera_restore_error"] = restore_error
            state["ok"] = False
        _RUN_LOCK.release()


def run_cleanup(*, dispatch, cleanup_id, thread_id, ownership_batches=None, stamp_guids=None,
                custom_assets=None, cell_m=25, dry_run=True, sleep=time.sleep, clock=time.monotonic,
                tick_seconds=1.0, wait_seconds=30.0):
    """Remove -> tick -> commit -> verify -> exact save -> reference checks -> explicit assets."""
    if not _RUN_LOCK.acquire(blocking=False):
        return {"ok": False, "error": "Another zone runner is active"}
    state = {}
    try:
        key = dict(cleanup_id=cleanup_id, thread_id=thread_id)
        request = dict(ownership_batches=ownership_batches, stamp_guids=stamp_guids,
                       custom_assets=custom_assets, cell_m=cell_m)
        if dry_run:
            return dispatch("prk_zone_cleanup", "cleanup", **key, **request, action="preview", dry_run=True)

        def stage(action, **arguments):
            deadline = clock()+wait_seconds
            while True:
                result = dispatch("prk_zone_cleanup", "cleanup", **key, action=action, dry_run=False, **arguments)
                if result.get("ok", result.get("success", False)):
                    return result
                pending = result.get("error") in {"Brush rendering is pending; retry on a later editor tick",
                    "Asset registry is loading; retry reference checks on a later tick"}
                if not pending or clock() >= deadline:
                    raise RuntimeError(str(result))
                sleep(tick_seconds)

        result = dispatch("prk_zone_cleanup", "cleanup", **key, action="status", dry_run=False)
        if result.get("ok", result.get("success", False)):
            state = result
            if stamp_guids is not None and sorted(v.replace("-", "").upper() for v in stamp_guids) != state["requested_guids"]:
                raise ValueError("Cleanup GUID list changed; use a new cleanup ID")
            if custom_assets is not None and sorted(custom_assets) != state["requested_assets"]:
                raise ValueError("Cleanup asset list changed; use a new cleanup ID")
            # Prepare checks the explicit asset set against the existing frozen receipt.
            if state["phase"] == "prepared":
                state = stage("prepare", **request)
        elif result.get("error", "").startswith("[Errno 2]"):
            state = stage("prepare", **request)
        else:
            raise RuntimeError(str(result))
        for _ in range(len(state["requested_guids"])+1):
            if state["phase"] not in {"prepared", "applying"}:
                break
            previous = len(state["removed_guids"])
            state = stage("apply_next")
            if len(state["removed_guids"]) <= previous:
                raise RuntimeError("Cleanup made no progress; inspect the same receipt")
            sleep(tick_seconds)
        for phase, action in (("applied", "commit"), ("committed", "verify"), ("verified", "save"),
                              ("saving", "save"), ("saved", "delete_assets"), ("deleting_assets", "delete_assets")):
            if state["phase"] == phase:
                state = stage(action)
                sleep(tick_seconds)
        return state
    except Exception as error:
        return {"ok": False, "error": str(error), "phase": state.get("phase"),
                "journal": state.get("journal"), "resume_same_cleanup_id": True}
    finally:
        _RUN_LOCK.release()
