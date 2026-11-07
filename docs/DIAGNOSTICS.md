# Diagnostic sketch

[Project](../README.md) · [Hardware map](HARDWARE.md) · [Source](../blinkee_iot_diag.ino)

## Build and connection

The sketch uses the Arduino API, `Wire` and STM32 `HardwareSerial`. Use an STM32 Arduino core and a target/variant matching **STM32F030CCT6** and the required pin/peripheral mapping. A board profile and core version are not pinned in this repository.

Open `blinkee_iot_diag.ino` as an Arduino sketch. Its sketch folder must be named `blinkee_iot_diag`. Verify the pin declarations and save a backup of the original firmware before writing the diagnostic image.

The service terminal uses **115200 baud**:

| Board signal | USB–UART connection |
| --- | --- |
| PB10 TX | Adapter RX |
| PB11 RX | Adapter TX |
| GND | Adapter GND |

Use 3.3 V UART signal levels and keep the adapter away from the battery-supply pin. The PB10/PB11 service assignment and the peripheral pin mappings still require verification on the target board.

## Commands

Commands are single characters read by `loop()`.

| Key | Action |
| --- | --- |
| `h` | Print help |
| `v` | Read backup/main ADC values and estimated voltages |
| `i` | Read CTS, GNSS TIMEPULSE and accelerometer interrupt inputs |
| `s` | Scan the I²C bus |
| `a` | Run the KX124 register/XYZ diagnostic |
| `g` | Scan GNSS output at 9600, 38400 and 115200 baud |
| `1` / `0` | Turn GNSS power on / off |
| `m` | Try basic modem AT diagnostics at several baud rates |
| `p` | Pulse modem PWR_ON |
| `r` | Pulse modem RESET |
| `b` | Bridge service UART to the modem at 115200 baud |
| `w` | Pulse scooter WAKEUP |
| `x` | Sound the siren briefly |

Send `~~~` to leave modem bridge mode. The sketch includes active control commands as well as readouts; power, reset, wakeup and siren actions affect the connected hardware.

## Configuration assumptions

| Setting | Current value | Meaning |
| --- | --- | --- |
| `ADC_VREF` | `3.30f` | Assumed ADC reference |
| `DIVIDER_IOT_BAT` | `3.20f` | Example backup-battery divider ratio |
| `DIVIDER_MAIN_BAT` | `15.00f` | Example main-battery divider ratio |
| `MODEM_PWR_ON_ACTIVE_LOW` | `true` | Assumed PWR_ON polarity |
| `MODEM_PWR_ON_PULSE_MS` | `2500` | PWR_ON pulse duration |
| `MODEM_RESET_ACTIVE_LOW` | `true` | Assumed reset polarity |
| `MODEM_RESET_PULSE_MS` | `200` | Reset pulse duration |

Calibrate the voltage-divider ratios before treating the displayed voltages as measurements. Verify modem polarity/timing and the board variant before running the active diagnostics. Software checks alone do not validate these hardware assumptions.
