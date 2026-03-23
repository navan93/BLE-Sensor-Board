# AI Agents Guide for This Repository

This repository contains the firmware and hardware design for a BLE Sensor Board based on the nRF52805, using Apache Mynewt and NimBLE.

This document is intended for both humans and AI coding agents working on this repo. It captures expectations and conventions so automated changes stay safe and consistent.

---

## Project Layout (high level)

- `FW/nrf52805-adv/` – Mynewt-based firmware project
  - `apps/blehid_demo/` – BLE HID keyboard demo application
  - `apps/ble_adv/` – Simple advertising demo
  - `hw/bsp/nrf52805/` – Board Support Package for the Sensor Board
  - `targets/` – Mynewt build targets (e.g. `nrf52805_hid`)
- `PCB/` – KiCad hardware design files for the board

Agents should prefer working within `FW/nrf52805-adv/` when modifying firmware behavior.

---

## General Guidelines for Agents

1. **Be conservative with changes**
   - Prefer minimal diffs that solve the specific request.
   - Avoid refactors unless explicitly asked.

2. **Build and tooling
   - Use the Mynewt `newt` tool for builds and flashing, e.g.:
     - `newt build nrf52805_hid`
     - `newt load nrf52805_hid`
   - Do not introduce new build systems.

3. **Package and dependency management**
   - For Mynewt, dependencies are managed via `pkg.yml` and `syscfg.yml` within the existing `project.yml` / repository layout.
   - Do **not** add unrelated third-party code or large libraries without a clear justification.

4. **Safety and persistence**
   - The board has limited flash and RAM (nRF52805). Be mindful of code size and heap usage.
   - Bonding and configuration persistence use Mynewt `sys/config` and NimBLE `store/config` over FCB in the BSP-defined flash area. Do not repurpose these flash regions without care.

5. **Style and structure**
   - Follow existing Mynewt examples in this tree for code style (e.g., use `static` for file-local functions, follow existing logging patterns with `MODLOG_DFLT`).
   - Keep application code simple and readable; avoid deeply nested abstractions.

6. **Testing and verification**
   - When possible, add or adapt small tests or debug logging to validate changes.
   - For BLE features, verify basic behaviors: advertising, connecting, pairing/bonding, and GATT operations.

---

## BLE / HID Specific Notes

- HID keyboard behavior currently lives in `FW/nrf52805-adv/apps/blehid_demo/src/main.c`.
- There is a generic helper `hid_send_keycode(uint8_t keycode)` that sends a key press and release over the HID Input Report.
- Pairing/bonding is configured for "Just Works" with bonding and encryption; bonds are persisted to internal flash.

Agents modifying BLE or HID behavior should:

- Preserve the existing security model (bond + encrypt, no passkeys) unless the user explicitly requests changes.
- Keep the HID report descriptor consistent with standard 8-byte keyboard reports.

---

## When in Doubt

If an automated change would:

- Alter flash layout or linker scripts,
- Change security characteristics (pairing, bonding, encryption requirements), or
- Introduce nontrivial dependencies or protocols,

then the agent should surface a clear plan and request explicit human confirmation before proceeding.

