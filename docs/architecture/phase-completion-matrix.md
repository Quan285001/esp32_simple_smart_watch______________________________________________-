# Phase Completion and Acceptance Matrix

**Working branch:** `rebuild/watch-face-studio-v2`  
**Purpose:** prevent implementation activity from being mistaken for phase completion.

A phase is complete only when its scope is implemented, its acceptance checks pass, its documentation reflects reality, and any hardware-only evidence is explicitly recorded. Work already present in later phases may remain in the branch, but phase ownership and acceptance are tracked sequentially.

| Phase | Scope | Current assessment | Remaining acceptance |
|---|---|---|---|
| 0 — Repository audit | Inventory, constraints, dependency and risk audit | Implemented; audit records exist | Re-audit only when scope or architecture materially changes |
| 1 — Architecture foundation | Module ownership, runtime lifecycle, contracts | Implemented in source with documented transitional boundaries | Review remaining wrappers and enforce ownership boundaries |
| 2 — Web App/PWA foundation | Editor, local project persistence, portable import/export, BLE connection workflow | Substantial implementation; not fully release-accepted | Complete end-to-end editor UX review, offline/project recovery cases and browser validation |
| 3 — Watch Face Engine | Model, parser, renderer, canvas, dynamic redraw and compatibility | Core implementation and host checks exist | Complete schema-to-preview-to-firmware parity matrix; document unsupported properties and compare visual output on hardware |
| 4 — Image/Bitmap pipeline | Source asset storage, conversion, WFS packaging, firmware streaming | RGB565 pipeline exists | Verify dimensions, byte limits, image decode/import errors, memory behavior and real-device image transfer/render |
| 5 — Firmware rebuild | App runtime, services, display, storage, power and diagnostics | Major refactor implemented; build/host checks exist | Final ownership audit, integration regression and runtime measurements on target |
| 6 — Communication/sync | Legacy BLE, WFS framing, capability exchange, transfer lifecycle | WFS v1 and compatibility paths implemented; CI/host tests exist | Physical browser/device handshake, repeated reconnect, transfer retry/cancel/disconnect and BLE security review |
| 7 — Hardware integration | Flash, TFT, button, Wi-Fi, BLE, storage, sleep and resource stress | Pending physical-board evidence | Complete `hardware-regression-checklist.md` and attach logs/results |
| 8 — Polish/release | UX polish, documentation, final regression and release artifact | Documentation is being consolidated; not release-complete | Resolve release blockers, clean full CI, hardware sign-off, known-issue and version/release notes |

## Execution policy

1. Work on the earliest phase with an unmet software acceptance criterion.
2. Do not begin optional work in later phases while an earlier phase has a known blocking defect.
3. A later-phase dependency may receive a narrowly scoped fix if it blocks validation of the current phase; record why.
4. Keep commits small and phase-coherent. Do not combine unrelated refactors and features.
5. Run the narrowest relevant tests after each change, then the complete host suite and CI at phase gates.
6. Never mark physical tests complete from source inspection or CI.
7. Preserve the working branch; do not merge into `main` without a separate explicit instruction.

## Current next focus

Complete the Phase 2 acceptance review and close any real editor/project workflow gaps before taking Phase 3 through its parity matrix. Do not infer completion merely from the existence of source files or a passing firmware build.
