# Mandeye Internal Lepton

Mandeye integration for FLIR Lepton thermal cameras connected via Raspberry Pi Pico over USB-CDC.
Captures and saves thermal frames during active scans, with one output directory per device.

## How it works

1. Auto-discovers connected Raspberry Pi Pico devices (or uses `--device` arguments).
2. Subscribes to the Mandeye ZeroMQ status socket.
3. While mode is `SCANNING`, each Pico thread captures frames and saves them to:
   ```
   <continuousScanTarget>/LEPTON_<simplifiedPicoName>/<timestamp_ns>.png
   <continuousScanTarget>/LEPTON_<simplifiedPicoName>/<timestamp_ns>_visual.png
   <continuousScanTarget>/LEPTON_<simplifiedPicoName>/<timestamp_ns>.meta.json
   ```
4. Saves at most `--fps` frames per second per device (default: 5).

### Output files per frame

| File | Description |
|------|-------------|
| `<ts>.png` | 16-bit grayscale PNG — raw radiometric values (CV_16UC1) |
| `<ts>_visual.png` | 8-bit JET colormap PNG — for quick visual inspection |
| `<ts>.meta.json` | Metadata: timestamp, device, resolution, FPA/housing temperature, frame counter |

## Hardware Requirements

- Raspberry Pi with USB port
- FLIR Lepton 3.5 camera module on a Raspberry Pi Pico breakout board
- The Pico must run the Lepton CDC firmware (presents as a USB serial/CDC device)

## Software Dependencies

```bash
sudo apt install libopencv-dev libgpiod-dev libserial-dev
```

`libserial-dev` is required for `cdc_viewer/readCDC`. If it is not available, `cdc_viewer` and `mandeye` are skipped automatically by CMake.

- CMake 3.20+, C++20 compiler
- `nlohmann/json` — bundled
- Google Test — fetched automatically via CMake FetchContent

## Build

Built as part of the main CMake tree. Binary output: `mandeye_lepton`.

```bash
cmake -B build -S extras/internal_lepton
cmake --build build --target mandeye_lepton
```

## Usage

```
mandeye_lepton [--device <path>] [--fps <n>] [--list]

  --device <path>   Add a specific CDC device (repeatable for multiple cameras)
  --fps <n>         Max frames per second to save per device (default: 5.0)
  --list            List detected Raspberry Pi Pico devices and exit
```

If no `--device` is given, all connected Picos are auto-discovered.

### Examples

```bash
# Auto-discover all Picos, default 5 fps
mandeye_lepton

# Explicit device, 8 fps
mandeye_lepton --device /dev/ttyACM0 --fps 8

# Discover and list available devices
mandeye_lepton --list
```

## Install and start the service

```bash
sudo cp mandeye/services/mandeye_lepton.service /usr/lib/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable mandeye_lepton
sudo systemctl start  mandeye_lepton
```

The service waits 20 s after boot to allow USB enumeration before starting.

## Check status

```bash
sudo systemctl status mandeye_lepton
sudo journalctl -fu mandeye_lepton
```

## Troubleshooting

**No Pico found**
```bash
mandeye_lepton --list
ls /dev/ttyACM*
```

**Frames skipped (previous frame not saved yet)**
Reduce `--fps` or check disk write speed.

## Supporting Tools

### cdc_viewer

Live viewer for Lepton cameras connected via Raspberry Pi Pico USB-CDC. Two binaries are built:

| Binary        | Source            | Description                                                                                                                         |
|---------------|-------------------|-------------------------------------------------------------------------------------------------------------------------------------|
| `readCDC`     | `readCDC.cpp`     | Single-device CLI viewer — opens a hardcoded Pico serial port, renders frames in a JET-colormap OpenCV window, records to MJPG AVI |
| `readCDC_gui` | `readCDC_gui.cpp` | Multi-device GUI viewer — auto-discovers all connected Picos, opens one window per device, records per-device AVI                  |

Both tools display:
- JET colormap thermal image (640×480 upscaled)
- Mouse crosshair with pixel temperature in °C
- Telemetry overlay: uptime, frame counter, FPA temperature, housing temperature, frame mean

`readCDC_gui` usage:
```bash
readCDC_gui                          # auto-discover all Picos
readCDC_gui --device /dev/ttyACM0   # specific device
```
Press **ESC** to quit.

Output video files are written to the working directory as `lepton_<device_suffix>_<timestamp>.avi`.

### spi_viewer

Diagnostic and capture tools for the direct SPI interface (when the Lepton is connected via SPI/I2C/GPIO rather than through a Pico).

| Binary           | Source               | Description                                                                                                                                                                    |
|------------------|----------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `testLeptonApp`  | `testLeptonApp.cpp`  | Full capture test — initialises GPIO (VSYNC pin 21), SPI (`/dev/spidev0.0` at 25 MHz) and I2C, runs for 60 s, saves raw `.bin` frames, 16-bit PNGs, and a JET-colormap AVI  |
| `leptonRawRead`  | `leptonRawRead.cpp`  | Low-level SPI diagnostic — reads raw VoSPI packets (164 bytes each) and dumps hex to stdout; useful for verifying SPI wiring and signal integrity before using higher-level tools |

`testLeptonApp` requires hardware SPI+I2C+GPIO and root / `CAP_SYS_NICE` for real-time scheduling.

```bash
./testLeptonApp    # runs for 60 s, writes frame*.png, frame*.bin, output.avi
./leptonRawRead    # streams raw hex packets to stdout
```

## License

All rights reserved by the author (Michał Pełka).
No use, copying, or distribution without explicit written permission.