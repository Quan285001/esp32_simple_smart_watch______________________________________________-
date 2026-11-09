# ESP32-C3 Hardware Regression Checklist

**Target:** ESP32-C3 DevKitM-1 + GC9A01 240×240  
**Environment:** PlatformIO `c3_clock`  
**Serial:** 115200 baud

This checklist is for the physical-board pass. CI verifies source contracts and builds the firmware; it cannot validate radio behavior, panel rendering, real heap pressure or wake reliability.

## 1. Flash and establish a baseline

1. Connect the board and confirm the PlatformIO upload port.
2. Build and flash:
   ```sh
   pio run -e c3_clock -t upload
   ```
3. Open the serial monitor:
   ```sh
   pio device monitor -b 115200
   ```
4. Capture the complete cold-boot log.
5. Confirm the boot terminal appears, startup reaches the clock UI, and no reset loop occurs.
6. Record the `[MEM] boot` and periodic snapshots.

Expected memory fields:

- `free`: currently available 8-bit-capable heap
- `min_free`: minimum heap observed since boot
- `largest_block`: largest currently allocatable 8-bit-capable block
- `fragmentation`: estimate derived from `free - largest_block`
- `task_stack_hwm`: minimum remaining stack space for the task that emitted the log, in bytes on ESP-IDF

The boot-network and weather-fetch tasks should also emit `bootNetwork task exit` and `weatherFetch task exit` snapshots when those tasks run.

## 2. Display and input

- [ ] Clock face draws correctly after cold boot.
- [ ] Short press switches between clock and weather.
- [ ] Long press opens the menu.
- [ ] Short press moves the selection; long press selects.
- [ ] Return items work in each submenu.
- [ ] Weather animation remains stable without visible flashing or persistent tearing.
- [ ] Repeated screen changes do not leave stale pixels or freeze the UI.

## 3. Wi-Fi, time and weather

- [ ] Wi-Fi connects with saved credentials.
- [ ] NTP updates the clock and local time remains stable after Wi-Fi is disabled.
- [ ] Weather fetch updates the forecast and cache.
- [ ] Reboot offline: the saved clock checkpoint and weather cache remain usable.
- [ ] Start a Wi-Fi scan while idle; verify the UI remains responsive.
- [ ] Try a scan during weather fetch and a weather fetch during scan; verify the coordinator rejects/defer requests rather than running conflicting radio work.
- [ ] Disable/re-enable Wi-Fi repeatedly; confirm reconnect monitoring does not stall the UI.

## 4. BLE lifecycle

- [ ] Enable Bluetooth from the menu and discover `ESP32_Watch_C3`.
- [ ] Connect from the Web editor and complete the WFS v1 Hello/HelloAck handshake.
- [ ] Confirm OS pairing/bonding and encrypted access to protected characteristics. Note that current Just Works mode encrypts the link but does not provide MITM protection.
- [ ] Read capabilities, device status and face status.
- [ ] Send a fresh HELLO (session ID zero) during an active test transfer; verify the previous cancellable transfer is aborted before a new session is accepted.
- [ ] Confirm the compact element list includes `t,dc,ac,d,w,s,i` and weather-condition support is reported as `wc=1`.
- [ ] Disconnect and reconnect at least 10 times.
- [ ] Confirm advertising resumes after disconnect when Bluetooth is enabled.
- [ ] Disable Bluetooth while disconnected; confirm advertising stops.
- [ ] Re-enable Bluetooth and reconnect.
- [ ] Disable Bluetooth while a client is connected; verify the current connection is not corrupted and advertising stays off after disconnect.
- [ ] While still connected after disabling, enable Bluetooth again; verify advertising restart is deferred until the client disconnects.
- [ ] Confirm legacy BLE commands still work.

## 5. Watch Face transfer transaction

Use a known-good small project first, then a project containing one RGB565 image.

- [ ] Upload a valid small project; verify TransferBegin, sequential chunk ACKs, digest/package verification and explicit Apply.
- [ ] Upload an opaque image-bearing project; confirm RGB565 rendering, positioning, clipping and colors on the round panel.
- [ ] Confirm transparent overlay images that require alpha compositing are rejected by Web preflight.
- [ ] If practical, force or simulate one lost DATA_ACK; verify the exact same DATA frame is retried and the chunk is not written twice.
- [ ] Capture `[PERF] WFS receive=... verify=... bytes=... heap_start=... heap_after=... min_heap_drop=...` and `[PERF] WFS apply=... total=...` logs; record the timings and heap values.
- [ ] Confirm the new face becomes active only after Apply succeeds.
- [ ] Start another upload and cancel it; verify a cancellation event is emitted only when cancellation actually succeeds.
- [ ] Send a wrong transfer ID to Cancel; verify an error is returned and the active transfer remains intact.
- [ ] Disconnect during Receiving; reconnect and verify the old session cannot continue writing chunks.
- [ ] Reconnect and upload a fresh project; verify the inactive slot can be reused.
- [ ] Try an invalid digest or malformed package; verify it is rejected and the previously active face remains intact.
- [ ] Reboot after a successful Apply; verify the committed face is restored.
- [ ] Reboot after an interrupted transfer; verify the previously committed face is restored when one exists. On a first install with no committed face, verify the built-in fallback is used.
- [ ] Test a malformed transfer ID/package length/digest type; verify rejection occurs before storage reservation and the active face remains unchanged.

## 6. Resource arbitration

- [ ] Attempt a Watch Face transfer during weather fetch; expect a resource-busy rejection.
- [ ] Attempt weather sync during transfer; expect a busy/deferred result.
- [ ] Attempt Wi-Fi credential changes during weather fetch or transfer; expect a rejection.
- [ ] Attempt a location update during Wi-Fi scan or weather fetch; expect a rejection.
- [ ] Confirm no command causes a reboot, UI freeze or corrupted weather location.

## 7. Sleep and wake

- [ ] Enter Light Sleep from the menu.
- [ ] Wake using the physical BOOT button.
- [ ] Repeat sleep/wake at least five times.
- [ ] Verify automatic Light Sleep occurs only on the clock screen after the configured idle timeout.
- [ ] Verify Auto Sleep is suppressed while BLE is connected or background network/scan work is active.
- [ ] Verify manual sleep shuts down Wi-Fi/BLE and wake leaves both radios off until explicitly enabled.
- [ ] Test BLE `SLEEP_ON` and physical-button wake.
- [ ] After wake, verify the clock, display and BLE stack can be reinitialized.
- [ ] Verify Wi-Fi and Bluetooth toggles still work after wake.

## 8. Memory and stability evidence

Capture serial snapshots:

1. After cold boot and startup completion.
2. After NTP and weather/lunar fetch.
3. After BLE connect/disconnect cycles.
4. After a successful image-bearing Watch Face transfer; preserve `[MEM] WFS receive begin`, `WFS verified` and `WFS apply complete` snapshots.
5. After repeated screen transitions.
6. After sleep/wake cycles.

Compare `min_free`, `largest_block`, `fragmentation` and each task's `task_stack_hwm`. Do not reduce the 8192-byte worker stack allocations until real worst-case measurements show adequate headroom with a safety margin.

## 9. Failure report format

For any failure, preserve:

- Exact steps immediately before the failure
- Full serial log from the last successful boot marker
- Screen state and whether BLE/Wi-Fi was connected
- Whether a Watch Face transfer was active
- Latest `[MEM]` snapshots
- Whether the board reset, froze, lost display output or merely rejected a command

**Hardware status:** pending physical-board execution. CI cannot perform these checks. The board owner must record the date, firmware commit, results, and captured logs before marking Phase 7 complete.
