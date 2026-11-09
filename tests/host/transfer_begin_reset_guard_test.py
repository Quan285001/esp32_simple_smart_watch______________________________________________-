#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "communication" / "transfer_manager.cpp"
source = SOURCE.read_text(encoding="utf-8")

begin_start = source.index("bool begin(")
begin_end = source.index("\nbool writeChunk(", begin_start)
begin_body = source[begin_start:begin_end]

busy_check = begin_body.index("if (isBusyState(currentState))")
reset_fields = begin_body.index("clearTransferFields();")
reset_state = begin_body.index("currentState = State::Idle;", reset_fields)
reset_error = begin_body.index("currentError = Error::None;", reset_state)
request_validation = begin_body.index("if (transferId == 0")

assert busy_check < reset_fields < reset_state < reset_error < request_validation, (
    "begin() must preserve active sessions, then clear stale attempt metadata "
    "before validating a new request"
)
assert "currentError = Error::Busy;" in begin_body
assert "return false;" in begin_body[busy_check:reset_fields]

abort_start = source.index("void abortSession()")
abort_end = source.index("\nSnapshot snapshot()", abort_start)
abort_body = source[abort_start:abort_end]
assert "currentTransferId == 0 || !canCancel(currentState)" in abort_body, (
    "disconnect without an owned transfer must not mutate a failed begin attempt"
)
assert abort_body.index("currentTransferId == 0") < abort_body.index("cancel(transferId)")

cleanup_failure = abort_body.index("if (!cancel(transferId))")
session_lost = abort_body.index("currentError = Error::SessionLost;")
cleanup_return = abort_body.index("return;", cleanup_failure)
assert cleanup_failure < cleanup_return < session_lost, (
    "disconnect cleanup failures must retain their concrete error instead of "
    "being overwritten by SessionLost"
)
assert "errorName(currentError)" in abort_body


print("WFS begin-attempt metadata reset guard passed.")
