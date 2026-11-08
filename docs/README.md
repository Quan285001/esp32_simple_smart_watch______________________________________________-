# Technical documentation index

This directory contains the engineering reference for the ESP32-C3 Watch Face Studio rebuild.

## Recommended reading order

1. [Technical Reference](TECHNICAL_REFERENCE.md) — system overview, module ownership, hardware/build setup, Web/firmware contract, storage, diagnostics and verification boundaries.
2. [Phase Completion Matrix](architecture/phase-completion-matrix.md) — phase acceptance criteria and current completion assessment.
3. [Firmware Rebuild Status](architecture/rebuild-implementation-status.md) — implementation record, known gaps and verification history.
4. [Phase 1 Architecture Contract](architecture/phase-1-architecture-contract.md) — module ownership, lifecycle and runtime invariants.
5. [Phase 2 Display Canvas](architecture/phase-2-display-canvas.md) — display abstraction and renderer boundary.
6. [WFS BLE Protocol v1](protocol/protocol-v1-draft.md) — GATT UUIDs, frame format, messages, session and transfer semantics.
7. [WFS1 Package Format](package/wface-v1.md) — binary container, manifest, assets, integrity and atomic installation.
8. [Portable Project Format](architecture/portable-project-format.md) — browser/editor project import/export and image asset bundling.
9. [Hardware Regression Checklist](architecture/hardware-regression-checklist.md) — required physical ESP32-C3 test procedure.
10. [Phase 2 Editor MVP](frontend/phase-2-editor-mvp.md) — editor workflow and MVP scope.

## Source-of-truth rule

- Shared JSON contracts live in `shared/schemas/`.
- The active device profile lives in `shared/capabilities/`.
- Firmware implementation lives in `src/`.
- Browser implementation lives in `web/` plus the root `index.html`.
- Protocol and package behavior are specified by the protocol and package documents above.
- When documentation conflicts with current source and tests, do not silently assume either is correct: inspect the implementation, resolve the discrepancy, then update the source contract and documentation together.

## Verification terminology

- **Host-tested:** a test executable/script ran in the host CI environment.
- **Build-verified:** the PlatformIO ESP32-C3 target compiled successfully.
- **CI-passed:** all jobs in the named workflow run completed successfully.
- **Hardware-verified:** the behavior was exercised on a physical ESP32-C3/GC9A01 with logs/results recorded.

These terms are not interchangeable. CI does not imply hardware verification.
