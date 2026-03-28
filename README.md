# Elmot Smart Servo

Closed-loop servo controller with browser-based configuration UI.

## Hardware

- **MCU**: Raspberry Pi Pico (RP2040, dual-core Cortex-M0+)
- **Angle sensor**: AS5600 12-bit magnetic encoder (I2C)
- **Motor driver**: DRV8833 dual H-bridge (PWM)
- **Input**: Standard RC servo PWM signal (1000-2500 us)

### Pin assignments

| Function    | GPIO | Description                |
|-------------|------|----------------------------|
| I2C SDA     | 26   | AS5600 data                |
| I2C SCL     | 27   | AS5600 clock               |
| Motor A     | 16   | DRV8833 channel A (PWM)    |
| Motor B     | 17   | DRV8833 channel B (PWM)    |
| PWM input   | 21   | RC servo signal            |
| LED         | 25   | Status indicator           |

## Architecture

- **Core 0**: USB stack (TinyUSB), parameter handling, telemetry logging
- **Core 1**: Servo control loop (angle read, PID-like control, motor PWM output)

### USB composite device (VID 0xCAFE, PID 0x4002)

| Interface | Class           | Purpose                             |
|-----------|-----------------|-------------------------------------|
| 0         | Vendor (WinUSB) | WebUSB: params, logs, commands      |
| 1         | MSC             | Read-only FAT16 disk with config/UI |

### Vendor requests (EP0)

| Code | Direction | Purpose                          |
|------|-----------|----------------------------------|
| 0x01 | IN        | WebUSB URL descriptor            |
| 0x02 | IN        | MS OS 2.0 descriptor set         |
| 0x03 | IN        | Read parameters (key=value text) |
| 0x04 | OUT       | Reboot device                    |
| 0x05 | OUT       | Save parameters to flash         |

### Bulk endpoints

- **EP 0x01 OUT**: Write parameters (key=value text)
- **EP 0x81 IN**: Telemetry log stream (text lines)

## Parameters

All parameters are stored as `key=value` text, transferred over USB, and persisted in the last sector of flash. The `version` key is read-only.

| Parameter             | Default | Unit    | Description                           |
|-----------------------|---------|---------|---------------------------------------|
| zero_restricted_angle | 5       | degrees | Dead zone around 0/360                |
| angle_tolerance       | 2       | degrees | Stop threshold (while moving)         |
| dead_angle            | 5       | degrees | Start threshold (from standstill)     |
| slow_angle            | 20      | degrees | Switch fast/slow speed threshold      |
| angle_reversed        | 0       | bool    | Reverse sensor direction              |
| cutoff_pwm            | 50      | %       | Below this, motor output is zero      |
| slow_pwm              | 70      | %       | Motor power when near target          |
| fast_pwm              | 100     | %       | Motor power when far from target      |
| slow_start_ms         | 200     | ms      | Ramp time on start / direction change |
| pwm_low_limit         | 1200    | us      | PWM input low end                     |
| pwm_high_limit        | 1700    | us      | PWM input high end                    |
| pwm_mock              | 0       | us      | Debug: override PWM input (volatile)  |

## Browser UI

The configuration UI is a single `index.htm` file. It can be accessed in three ways:

1. **From the device** — open the file served via the MSC virtual disk or navigate to the WebUSB landing page
2. **Online** — visit [smart-servo.elmot.xyz](https://smart-servo.elmot.xyz/) (published via GitHub Pages on every release)
3. **Locally** — open `index.htm` directly in a browser

The UI is a PWA and works offline once loaded. It provides:

- Visualization of angle parameters
- Sliders for all parameters with live update
- PWM power bars
- PWM mock input for testing without a signal source
- Real-time telemetry log
- Save to flash / Reset defaults / Restart device

### GitHub Pages deployment

A GitHub Actions workflow (`.github/workflows/static.yml`) publishes `index.htm` to GitHub Pages each time a release is created. The file is deployed under a path matching the release tag (e.g. `/<tag>/index.htm`). The custom domain `smart-servo.elmot.xyz` is configured via the `CNAME` file.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

The build fetches the Pico SDK automatically. Output: `rpi-servo-gui.uf2`

## Flashing

Hold BOOTSEL on the Pico, connect USB, copy `rpi-servo-gui.uf2` to the RPI-RP2 drive.

## Version

Set `SERVO_VERSION` in `CMakeLists.txt`. It propagates to USB descriptors, parameter text, and the virtual disk.
