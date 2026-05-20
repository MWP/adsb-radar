# adsb-radar

A lightweight retro-themed ADS-B radar display written in C, designed for Raspberry Pi 4/5.
Written using the SDL3 graphics library, it runs on an accelerated framebuffer device, under X11, or Wayland.
While designed for small-ish display use, it will also compile and run happily on a typical PC.

The software reads live aircraft data from a [readsb](https://github.com/wiedehopf/readsb) instance (included in build), or [dump1090-fa](https://github.com/flightaware/dump1090) (or similar). An attached serial GPS unit can optionally keep the map centred on the device's current position.

[![example adsb-radar image](exmaple-sm.png)](example.png)

---

## The Project (as a whole)

This software is part of a project to create a low-power, compact, standalone (offline - no internet access), fast-startup aircraft graphical monitoring station using a [Crystalfontz CFA050A0-PI-MBCT Raspberry Pi CM4/5 display](https://www.crystalfontz.com/product/cfa050a0pimbct-5-inch-720x1280-rpi-cm4-compatible-tft), and an RTL-SDR (or similar) USB device for ADS-B radio data reception.

---

## Software Features

- **Live aircraft tracking** — polls `aircraft.json` over HTTP or reads a local file directly
- **Altitude colouring** — dots use an HSV hue ramp (red → green → blue → purple) across 0–15 000 m
- **Heading lines** — proportional to ground speed, suppressed when speed is unknown
- **Position trails** — altitude-coloured, fading with age, up to 300 points per aircraft
- **Configurable map layers** — up to 16 GeoJSON or ESRI Shapefile layers with per-layer colour
- **Max-range circle** — tracks and draws the radius of the farthest aircraft seen
- **Aircraft list panel** — collapsible sidebar listing aircraft currently in the viewport
- **Serial GPS** — NMEA reader thread; re-centres the map on your own position when locked
- **Touch and mouse input** — zoom, pan, and button controls; safe on touch-only displays
- **Imperial and metric units** — nm/kt/ft or km/km·h/m, switchable in config
- **KMS/DRM framebuffer** — runs on a bare Raspberry Pi TTY with no desktop compositor

---

## Requirements

### Build host

```
sudo apt install build-essential cmake git libdrm-dev libgbm-dev libzstd-dev \
    zlib1g-dev librtlsdr-dev libusb-1.0-0-dev libncurses-dev \
	libsoapysdr-dev libhackrf-dev libbladerf-dev libad9361-dev libiio-dev \
	libudev-dev libinput-dev libudev1
```

All other dependencies (SDL3, SDL3\_ttf, FreeType, cJSON) are fetched and built automatically by CMake. No system SDL or TTF packages are needed or used.

### Runtime (Raspberry Pi)

- User must be in the `video` group for KMS/DRM framebuffer access
- Tested on Raspberry Pi 4 (Cortex-A72) and Raspberry Pi 5 (Cortex-A76)

---

## Building

```bash
# Desktop / debug build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build build -j$(nproc)

# Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build build -j$(nproc)
```

The build produces `build/adsb-radar` and `build/readsb`. Assets and the default config file are copied into the build directory automatically.

> After adding a new `.c` source file, re-run `cmake -S . -B build` to refresh the file glob before building.

### Cross-compiling for Raspberry Pi

```bash
sudo apt install gcc-aarch64-linux-gnu

# RPi 4 (Cortex-A72)
cmake -S . -B build-rpi \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-rpi -j$(nproc)

# RPi 5 (Cortex-A76)
cmake -S . -B build-rpi5 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DRPI_CPU=cortex-a76
cmake --build build-rpi5 -j$(nproc)
```

---

## Running

```bash
./build/adsb-radar
```

### Using the bundled readsb

The build includes a `readsb` binary. Run it alongside adsb-radar to decode from a connected RTL-SDR dongle:

```bash
./readsb --no-interactive --metric --write-json=/tmp --write-json-every=1 \
         --device-type rtlsdr --ppm=0
```

Then point adsb-radar at the output file:

```ini
[network]
local_json = /tmp/aircraft.json
```

Key options:

| Option | Purpose |
| --- | --- |
| `--no-interactive` | disable the ncurses UI |
| `--metric` | use metric units in readsb output |
| `--write-json=/tmp` | write `aircraft.json` to this directory every `--write-json-every` seconds |
| `--write-json-every=1` | update interval in seconds |
| `--device-type rtlsdr` | select RTL-SDR as the input device |
| `--ppm=0` | RTL-SDR frequency correction in parts per million |

## Configuration

Config is loaded from `./adsb-radar.ini`, then `~/.config/adsb-radar/adsb-radar.ini`, then built-in defaults. The bundled `adsb-radar.ini` is fully commented and documents every available key.

Key sections:

| Section | Purpose |
| --- | --- |
| `[network]` | dump1090/readsb host, port, poll interval, or local file path |
| `[display]` | window size, fullscreen, video driver, max-range circle |
| `[map]` | up to 16 GeoJSON/SHP layers with optional per-layer colour |
| `[view]` | map centre (lat/lon) and initial zoom level |
| `[aircraft]` | age/fade timeouts, trail length, dot size, heading line length |
| `[units]` | `imperial` (nm/kt/ft) or `metric` (km/km·h/m) |
| `[font]` | TTF font path and sizes for labels and the aircraft list |
| `[gps]` | serial port, baud rate, lock-on-start |
| `[colours]` | RGBA colours for every display element |

---

## Map data

Two GeoJSON files are included:

- `world_coastlines.geojson` — world coastline polygons
- `world_airports.geojson` — airport locations

Additional layers can be added in `[map]` using any GeoJSON FeatureCollection or ESRI Shapefile containing LineString, MultiLineString, Polygon, or MultiPolygon geometry.

The `world_airports.geojson` file was generated using [overpass-turbo.eu](https://overpass-turbo.eu) with the query:
```
[out:json][timeout:9000];
(
  way["aeroway"="runway"];
  way["aeroway"="aerodrome"]["iata"];
  relation["aeroway"="aerodrome"]["iata"];
);
out geom;
```

The `world_coastlines.geojson` file was sourced from [geojson-maps.kyd.au](https://geojson-maps.kyd.au).
These maps are derived from Natural Earth data and are in the public domain.

---

## Controls

Mouse and touchscreen are the primary input methods. All navigation is also available via keyboard.

### Mouse and touchscreen

| Input | Action |
| --- | --- |
| Drag | Pan the map |
| Scroll wheel | Zoom in/out |
| Two-finger pinch | Zoom in/out |
| Click / tap aircraft | (selects — see aircraft list) |

On-screen buttons are shown on the right side of the display and respond to both click and tap:

| Button | Action |
| --- | --- |
| `+` / `−` | Zoom in / zoom out |
| `↑` `↓` `←` `→` | Pan (directional pad) |
| `CTR` | Reset view to configured centre |
| `GPS` | Toggle GPS re-centring (shown only when GPS is enabled) |
| Aircraft list (top-right) | Expand/collapse the aircraft sidebar |
| `EXIT` (bottom-left) | Quit (shown only when `exit_button = true` in config) |

### Keyboard

| Key | Action |
| --- | --- |
| Arrow keys | Pan |
| `+` / `-` | Zoom in/out |
| `Home` / `C` | Reset to configured centre |
| `Q` / `Escape` | Quit |

---

## AI assistance

This project was developed with the assistance of [Claude Code](https://claude.ai/code) (Anthropic). Claude contributed to code architecture, build system configuration, documentation, and licence compliance review. All code was authored, reviewed, and directed by the project maintainer.

---

## License

[GNU General Public License v3.0 or later](LICENSE)

Copyright (C) 2026 Mark Williams
