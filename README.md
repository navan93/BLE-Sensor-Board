# BLE Sensor Board
This is a flex PCB sensor board which can be used with the [Sensor Watch](https://github.com/joeycastillo/Sensor-Watch/tree/main) board by Joey Castillo. This is based on Nordic Semi's nRF52805 which is a tiny BLE SoC which is compatible with a 2 layer PCB. This adds BLE capabilities to the already awesome Sensor Watch.

[nRF52805](https://docs.nordicsemi.com/bundle/nRF52805_PS_v1.4/resource/nRF52805_PS_v1.4.pdf) has:
 - 64 MHz Arm Cortex-M4
 - 192 KB Flash + 24 KB RAM

It is configured in internal LDO regulator setup.

> [!CAUTION]
> The hardware is a bare minimum design which will make this work. For e.g the ANT pin is connected straight to a chip antenna, skipping all the filter/matching circuit, do not expect this to pass any certifications!!

## Pin Map
| **nRF Pin** | **Pin** | **Digital** | **Interrupt**   | **Analog**    | **I2C**             | **SPI**              | **UART**                 | **PWM**  | **Ext. Wake** |
| :---------: | :-----: | :---------: | :-------------: | :-----------: | :-----------------: | :------------------: | :----------------------: | :------: | :-----------: |
| **P0.04**   | **A0**  | PB04        | EIC/EXTINT\[4\] | ADC/AIN\[12\] | —                   | —                    | —                        | —        | —             |
| **P0.05**   | **SCL** | —           | —               | —             | SCL<br>SERCOM1\[1\] | —                    | —                        | —        | —             |
| **P0.12**   | **SDA** | —           | —               | —             | SDA<br>SERCOM1\[0\] | —                    | —                        | —        | —             |
| **P0.16**   | **A1**  | PB01        | EIC/EXTINT\[1\] | ADC/AIN\[9\]  | —                   | SCK<br>SERCOM3\[3\]  | RX<br>SERCOM3\[3\]       | TC3\[1\] | —             |
| **P0.18**   | **A2**  | PB02        | EIC/EXTINT\[2\] | ADC/AIN\[10\] | —                   | MOSI<br>SERCOM3\[0\] | TX or RX<br>SERCOM3\[0\] | TC2\[0\] | RTC/IN\[1\]   |
| **NC**      | **A3**  | PB03        | EIC/EXTINT\[3\] | ADC/AIN\[11\] | —                   | CS<br>SERCOM3\[1\]   | RX<br>SERCOM3\[1\]       | TC2\[1\] | —             |
| **P0.21**   | **A4**  | PB00        | EIC/EXTINT\[0\] | ADC/AIN\[8\]  | —                   | MISO<br>SERCOM3\[2\] | TX or RX<br>SERCOM3\[2\] | TC3\[0\] | RTC/IN\[0\]   |

## Features
1. Time sync - TBD
2. OTA update for Sensor Watch - TBD
3. OTA for Sensor board - TBD
4. Wireless Button - TBD

## Firmware
The firmware for this board is based on the Apache Mynewt OS.

### Setup
#### Toolchain
Install arm-gcc from official ARM release https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#### Mynewt OS
TBD

### Build
```
cd FW
newt build
```

### Upload
```
newt load nrf52_blinky
```