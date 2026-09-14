# CardputerZero Compass

Compass app for M5Stack CardputerZero.

## Features

- Show compass heading with animated dial
- Show pitch/roll bubble level overlay
- Expand live accelerometer, gyroscope, and magnetometer values
- Calibrate hard-iron and cross-axis magnetic distortion with full 3D rotation
- Save and load calibration data between launches within the same device boot
- Require calibration on first use and confirm when calibration is saved

## Dependencies

Run the bootstrap script once after cloning this repository:

```bash
./bootstrap.sh
```

It creates `.venv/`, installs the Python build tools, fetches dependencies from
`repos.json`, and keeps all third-party source under `dependencies/`.

System packages expected by the build:

- CMake and a C/C++ compiler
- SDL2 development files for `COMPASS_USE_SDL=ON`
- `python3-venv` so `bootstrap.sh` can create `.venv/`
- `aarch64-linux-gnu-gcc/g++` for cross-building the CardputerZero package

On macOS, install the desktop build tools with Homebrew:

```bash
brew install cmake pkg-config sdl2
```

macOS supports the SDL desktop build only. Device framebuffer and Debian
packaging are Linux/CardputerZero targets.

Project dependencies pulled from `repos.json`:

- `lvgl`
- `spdlog`
- `smooth_ui_toolkit`

Image asset conversion runs during build and uses LVGL's Python converter. If
the converter dependencies are missing, install them in your Python environment:

```bash
./.venv/bin/python -m pip install pypng lz4 Pillow
```

Font conversion is not part of the normal build. If fonts are regenerated with
`src/assets/convert_fonts.py`, install `lv_font_conv` separately and keep it
available in `PATH`.

## Build

For Linux SDL testing:

```bash
cmake -S . -B build/sdl -DCOMPASS_USE_SDL=ON
cmake --build build/sdl -j8
```

For macOS SDL testing:

```bash
cmake -S . -B build/macos-sdl -DCOMPASS_USE_SDL=ON
cmake --build build/macos-sdl -j8
```

For CardputerZero framebuffer build:

```bash
cmake -S . -B build/cp0 -DCOMPASS_USE_SDL=OFF
cmake --build build/cp0 -j8
```

For cross build from x86 Linux with the GNU aarch64 toolchain:

```bash
cmake -S . -B build/cp0 \
  -DCOMPASS_USE_SDL=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
cmake --build build/cp0 -j8
```

The output binary is `dist/M5CardputerZero-Compass`.

## Usage

Run the SDL build:

```bash
./dist/M5CardputerZero-Compass
```

Calibration data is saved to `/tmp/Compass/calibration.conf` by default for both
SDL and CardputerZero builds. Launches within the same device boot reuse this
data. When the device reboots and `/tmp` is cleared, calibration is required again.
If no valid saved calibration exists, a required-calibration dialog appears.
Press Enter to start, then rotate the device through every direction, away from
metal and magnets. Calibration must pass the quality checks and be saved before
entering the compass. Esc exits the app during this required flow.
The completion dialog confirms that calibration was saved; press Enter to return
to the compass. Later launches reuse the saved calibration until temporary data
is cleared.
During calibration, coverage and captured sample count are shown below the progress
bar. The first automatic check runs after 20 seconds; unsuccessful checks retry
every 2 seconds while capture continues. Enter also attempts to finish and save.
Turn the device over and rotate around all three axes when prompted. Sensor
failures and magnetic interference have separate messages. Coverage is a motion
indicator, not a countdown to completion.

Override the calibration path for development (a persistent path keeps data
across reboots):

```bash
COMPASS_CALIBRATION_PATH=./calibration.conf ./dist/M5CardputerZero-Compass
```

Key controls:

- Compass page: `8` expand/collapse sensor info, `KEY_HELP` open help (`H` on SDL), hold `Esc` to exit
- Help overlay: `KEY_HELP` or `H` (SDL) or `Esc` close
- Expanded info: `7` open calibration, `8` collapse sensor info
- Calibration page: `6` or Enter starts/saves calibration; Enter returns after success
- Calibration page: `4` or Esc goes back for optional calibration; Esc exits during required calibration

## Package

Build the cp0/CardputerZero Debian package:

```bash
./packaging/deb/package_deb.sh
```

The package script is device-targeted only. It always configures the framebuffer
build and produces an `arm64` APPLaunch package.

The generated package is written to `dist/`:

```text
dist/m5cardputerzero-compass_0.2.4_m5stack1_arm64.deb
```
