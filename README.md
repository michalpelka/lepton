# Lepton Thermal Camera Library

A C++ library for interfacing with FLIR Lepton thermal imaging cameras on Raspberry Pi using SPI, I2C, and GPIO.

## Overview

This library provides a high-level C++ interface for controlling and capturing thermal images from FLIR Lepton cameras. It handles VoSPI (Video over SPI) protocol communication, frame synchronization via GPIO, and camera configuration through I2C.

## Features

- **SPI Communication**: High-speed video data acquisition via SPI interface
- **I2C Control**: Camera configuration and control using the Lepton SDK
- **GPIO Integration**: VSYNC and debug signal handling using libgpiod
- **Frame Processing**: VoSPI packet parsing and frame assembly
- **OpenCV Integration**: Direct output to OpenCV Mat format for display and processing
- **Multi-threaded Design**: Separate threads for capture and processing
- **Raw Data Access**: Support for raw frame data callbacks

## Hardware Requirements

- Raspberry Pi (any model with SPI support)
- FLIR Lepton thermal camera module (tested with Lepton 3.5)
- Proper electrical connections:
  - SPI (MISO, MOSI, SCLK, CS)
  - I2C (SDA, SCL)
  - GPIO for VSYNC (default: GPIO 21)
  - GPIO for debug (default: GPIO 20)

## Software Dependencies

### System Libraries
- **libgpiod** - GPIO device library
  ```bash
  sudo apt install libgpiod-dev
  ```

- **OpenCV** - Computer vision library (version 4.x recommended)
  ```bash
  sudo apt install libopencv-dev
  ```

### Build Tools
- CMake (version 3.20 or higher)
- C++20 compatible compiler (GCC 10+ or Clang 11+)

### Included Dependencies
- **Google Test** - Automatically fetched via CMake FetchContent
- **Lepton SDK** - Included in `leptonSDKEmb32PUB` subdirectory

## Building

```bash
# Clone the repository
git clone https://github.com/michalpelka/lepton.git
cd lepton

# Create build directory
mkdir build
cd build

# Configure and build
cmake ..
make

# Run tests (optional)
ctest
```

## Usage

### Basic Example

```cpp
#include "lepton.h"

int main() {
    lepton::Lepton cam;
    
    // Initialize GPIO (chip name, VSYNC pin, debug pin)
    if (!cam.initGpio("gpiochip0", 21, 20)) {
        std::cerr << "Failed to init GPIO" << std::endl;
        return 1;
    }
    
    // Start SPI communication
    if (!cam.startSpi("/dev/spidev0.0", 25000000)) {
        std::cerr << "Failed to start SPI" << std::endl;
        return 1;
    }
    
    // Start I2C communication
    if (!cam.startI2c()) {
        std::cerr << "Failed to start I2C" << std::endl;
        return 1;
    }
    
    // Configure camera GPIO
    if (!cam.configureOemGpio()) {
        std::cerr << "Failed to configure OEM GPIO" << std::endl;
        return 1;
    }
    
    // Set frame callback
    cam.setFrameCallback([](cv::Mat& img) {
        cv::imshow("Thermal Image", img);
        cv::waitKey(1);
    });
    
    // Start capturing
    cam.capture();
    
    // Keep running
    std::this_thread::sleep_for(std::chrono::minutes(1));
    
    // Clean shutdown
    cam.shutdown();
    return 0;
}
```

### Available Callbacks

The library provides three types of callbacks:

1. **Frame Callback** (scaled image):
   ```cpp
   cam.setFrameCallback([](cv::Mat& img) {
       // Process scaled thermal image
   });
   ```

2. **Frame Without Scale Callback** (raw thermal values):
   ```cpp
   cam.setFrameWithoutScale([](cv::Mat& img) {
       // Process raw thermal values
   });
   ```

3. **Raw Frame Data Callback**:
   ```cpp
   cam.setRawFrameCallback([](std::vector<uint8_t>& data) {
       // Process raw VoSPI data
   });
   ```

## Examples

The repository includes several example applications:

- **testLeptonApp** - Live camera capture with display (requires hardware)
- **readRawLepton** - Raw data reading utility

Build and run examples:
```bash
cd build
./testLeptonApp      # Live capture (requires hardware)
```

Note: `testLeptonFile` is currently not built as it requires an unimplemented function.

## VoSPI Protocol

The library implements the VoSPI (Video over SPI) protocol for Lepton cameras:
- Handles 4-segment telemetry frames
- CRC validation for packet integrity
- Automatic resynchronization on packet loss
- Segment assembly and frame reconstruction

## Testing

The project includes unit tests using Google Test:

```bash
cd build
./test_simple
```

Tests cover:
- VoSPI packet parsing
- CRC validation
- Segment and packet number extraction
- Discard packet detection

## SPI Configuration

Default SPI settings:
- Speed: 25 MHz
- Mode: CPOL | CPHA
- Bits per word: 8
- Device: `/dev/spidev0.0`

## GPIO Configuration

Default GPIO pins (BCM numbering):
- VSYNC: GPIO 21
- Debug: GPIO 20

## Raspberry Pi Setup

Enable SPI and I2C interfaces:
```bash
sudo raspi-config
# Navigate to Interface Options
# Enable SPI
# Enable I2C
# Reboot
```

## Real-time Performance

For optimal frame capture performance, the library supports real-time scheduling:
- Uses SCHED_FIFO scheduling policy
- Configurable priority (default: 20)
- Requires root privileges or CAP_SYS_NICE capability

## Troubleshooting

### SPI Communication Issues
- Verify SPI is enabled: `ls /dev/spi*`
- Check SPI permissions: `sudo chmod 666 /dev/spidev0.0`
- Reduce SPI speed if experiencing data corruption

### I2C Communication Issues
- Verify I2C is enabled: `ls /dev/i2c*`
- Check device address: `i2cdetect -y 1`
- Ensure proper pull-up resistors on I2C lines

### GPIO Issues
- Verify libgpiod is installed
- Check GPIO permissions
- Ensure correct chip name: `gpiodetect`

## License

This project is provided without any license. All rights are reserved by the author.

You may **not** use, copy, modify, merge, publish, distribute, sublicense, or sell any part of this software unless you obtain explicit written permission from the author.

## Contributing

Please contact the author for contribution guidelines.

## Author

Michał Pełka

## Acknowledgments

- FLIR Systems for the Lepton SDK
- VoSPI protocol implementation based on FLIR documentation
