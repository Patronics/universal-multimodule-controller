# Universal Multi-Module Controller

This project aims to allow standard input devices (such as game controllers and keyboards) to control output to 4-in-one multi-module transmitters.

Created by Patrick Leiser and Josh Adriano.



## Installation
Use the [Arduino-Pico board core](https://arduino-pico.readthedocs.io/en/latest/) by Earle Philhower
Dependencies:
U8g2 by oliver
ArduinoJson by Benoit Blanchon
Bounce2 by Thomas O Fredericks
Pico PIO USB by Sekigon-gonnoc

You can program it with the Arduino IDE directly, adjust the menu options as described at the top of `GamesteerRC.ino`.

Alternatively, for advanced usage and debugging, you can use the

## Build & Debug Scripts

---

### build.sh  
Builds a **release version** of the firmware using Arduino CLI.

- Optimized for performance (`-O2` / `-Os`)
- Produces `.elf` and `.uf2` in `build/`
- Use when you want final or production-like firmware

---

### debug-build.sh  
Builds a **debug version** of the firmware.

- Uses `-Og` optimization for good debugging behavior
- Includes full debug symbols (`-g`)
- Outputs to `build-debug/`
- Use when stepping through code or inspecting variables with GDB

---

### flash.sh  
Flashes firmware using the **Raspberry Pi Debug Probe (OpenOCD + SWD)**.

- Programs the `.elf` directly to flash
- Verifies and resets the device
- Use for fast iteration during debugging

---

### openocd.sh  
Starts the **OpenOCD GDB server**.

- Provides a debugging connection on port 3333
- Required before connecting GDB
- Use when preparing a debug session

---

### gdb.sh  
Launches **GDB for source-level debugging**.

- Connects to OpenOCD (`localhost:3333`)
- Uses the project `.elf`
- Use for breakpoints, stepping, and inspecting state
- See debugging section below for additional details
---

### upload.sh  
Flashes firmware using the **UF2 bootloader (USB mode)**.
- uses `arduino-cli upload`
- Before use, edit upload.sh with the port of your target device, such as `/dev/cu.usbmodem101` or `COM7`
  - Alternatively, you can manually boot the device into BOOTSEL mode, and copy `build/GamesteerRC.ino.uf2` onto the device.
- No debugger needed
- Use for simple deployment or recovery
---

### Typical usage

- **Development (debugging):** `debug-build.sh` → `flash.sh` → `openocd.sh` → *(in a second terminal session)* `gdb.sh`
- **Quick standalone flash:** `build.sh → upload.sh`

## Debugging
For debugging, once OpenOCD and GDB are active, within GDB run 
```gdb
target extended-remote :3333
monitor reset halt
break panic
continue

```