# OLED System Monitor

A project for the Raspberry Pi Pico 2 (RP2350) series that displays system metrics on an SSD1309 OLED display. 
It parses JSON objects received via USB or UART to show real-time CPU, RAM, and SWAP (Linux) / PAGE (Windows) usage as bar graphs, along with system load averages and uptime.

## Hardware Connections

| SSD1309 Pin | Pico 2 Pin | Function |
|-------------|------------|----------|
| VCC         | 3.3V       | Power    |
| GND         | GND        | Ground   |
| SDA         | GP4        | I2C0 SDA |
| SCL         | GP5        | I2C0 SCL |

*Note: Default I2C address is `0x3C`.*

## Requirements

- [Pico SDK 2.2.0+](https://github.com/raspberrypi/pico-sdk)
- [Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) or [Pico 2 W](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
- [CMake](https://cmake.org/) 3.13 or higher
- [ARM GCC](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm) 13+ Toolchain (`arm-none-eabi-gcc`)

## Building

1. Initialize the Pico SDK in your environment:
   
   **Linux:**
   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   ```

   **Windows:**
   ```bash
   set PICO_SDK_PATH=C:\path\to\pico-sdk
   ```

2. Configure and build with CMake.

   Release build:
   ```bash
   cmake -S . -B build-fw-release -DCMAKE_BUILD_TYPE=Release
   cmake --build build-fw-release
   ```

   Debug build:
   ```bash
   cmake -S . -B build-fw-debug -DCMAKE_BUILD_TYPE=Debug
   cmake --build build-fw-debug
   ```

3. The output file `pico2-oled-system-monitor.uf2` will be generated in the corresponding build directory.

## Usage

1. Connect the OLED display to the Pico 2 as specified in the [Hardware Connections](#hardware-connections) section.
2. Flash the `pico2-oled-system-monitor.uf2` file to your Pico 2.
3. Send a JSON object to the Pico 2 via USB Serial or UART0 at **115200 baud**.

### Expected JSON Format

The monitor expects a JSON object with the following structure:

```json
{
  "cpu": 25,
  "ram": 45,
  "swap": 10,
  "load": [0.15, 0.25, 0.30],
  "uptime": "2d 04:15:22"
}
```

- `cpu`, `ram`: Percentage values (0-100) for the bar graphs.
- `swap` (Linux) / `page` (Windows): Percentage value (0-100) for the SWAP/PAGE bar graph.
- `load`: An array of three floats representing load averages.
- `uptime`: A string representing the system uptime.

The display will automatically update whenever a complete JSON object is received.

## Linux Application

This repo also includes a small Linux userspace program that collects metrics and emits the JSON expected by the Pico firmware.

### Build (Linux)

```bash
cmake -S linux-host -B build-linux-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux-release

cmake -S linux-host -B build-linux-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux-debug
```

### Run

To write to the Pico's USB CDC serial device (example: `/dev/ttyACM0`) once per second:

```bash
./build-linux-release/Release/system_monitor_host -d /dev/ttyACM0 -i 1
```

To print the JSON to stdout (useful for debugging):

```bash
./build-linux-release/Release/system_monitor_host --stdout -i 1
```

## Windows Application

This repo also includes a small Windows userspace program that collects the same metrics and emits the JSON expected by the Pico firmware.

Notes:

- Windows does not provide Unix-style 1/5/15 minute load averages. On Windows the `load` is approximated as a smoothed value derived from overall CPU utilization scaled by logical core count.
- For COM ports `COM10` and above, you must use the `\\.\\COM10` path when opening the serial device.

### Build (Windows)

Using CMake:

```powershell
cmake -S windows-host -B build-windows-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows-release

cmake -S windows-host -B build-windows-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-windows-debug
```

### Run (Windows)

To write to the Pico's USB CDC serial port (example: `COM3`) once per second:

```powershell
./build-windows-release/Release/system_monitor_host.exe -d COM3 -i 1
```

To print JSON to stdout:

```powershell
./build-windows-release/Release/system_monitor_host.exe --stdout -i 1
```
