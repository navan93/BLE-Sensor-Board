# BLE HID Demo (nRF52805)

This is a minimal **BLE HID keyboard demo** for the nRF52805-based Sensor Board, using **Apache Mynewt** and **NimBLE**.

The app exposes a standard **HID Keyboard** over BLE and sends a single `'a'` keystroke when a connected central subscribes to the HID input report characteristic.

### Features

- Acts as a **BLE peripheral** with HID Keyboard appearance.
- Implements the HID service (0x1812) with:
  - HID Information (0x2A4A)
  - Report Map (0x2A4B)
  - HID Control Point (0x2A4C)
  - Keyboard Input Report (0x2A4D, 8‑byte report)
- On notification subscription, sends:
  - Key press for `'a'` (HID usage 0x04), then
  - Key release (all zeros).

### Building

From `FW/nrf52805-adv`:

```bash
newt build nrf52805_hid
```

### Flashing

```bash
newt load nrf52805_hid
```

(Assumes the `nrf52805_hid` target is already defined, as in this repo.)

### Usage

1. Power the board; it starts advertising as `Sensor Watch F91-W`.
2. On your PC or phone, scan for Bluetooth devices and connect to it.
3. When the OS enables notifications on the HID input report, the board will send one `'a'` keystroke to the host. Open a text editor to see it appear.
