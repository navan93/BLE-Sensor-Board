# BLE Sensor Board
This is a flex PCB sensor board which can be used with the [Sensor Watch](https://github.com/joeycastillo/Sensor-Watch/tree/main) board by Joey Castillo. This is based on Nordic Semi's nRF52805 which is a tiny BLE SoC which is compatible with a 2 layer PCB. This adds BLE capabilities to the already awesome Sensor Watch.

[nRF52805](https://docs.nordicsemi.com/bundle/nRF52805_PS_v1.4/resource/nRF52805_PS_v1.4.pdf) has:
 - 64 MHz Arm Cortex-M4
 - 192 KB Flash + 24 KB RAM

It is configured in internal LDO regulator setup.

> [!CAUTION]
> The hardware is a bare minimum design which will make this work. For e.g the ANT pin is connected straight to a chip antenna, skipping all the filter/matching circuit, do not expect this to pass any certifications!!

## Features
1. Time sync - TBD
2. OTA update for Sensor Watch - TBD
3. OTA for Sensor board - TBD
4. Wireless Button - TBD

## Firmware
The firmware for this board is based on the Apache Mynewt OS.

### Setup
TBD

### Build
```
cd FW
newt build
```

### Upload
TBD