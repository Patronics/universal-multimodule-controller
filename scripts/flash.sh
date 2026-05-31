#!/bin/bash
#flashes the target via openocd, only use when debugger is present

set -e
openocd -f openocd/rp2350-debugprobe.cfg -c "program build/GamesteerRC.ino.elf verify reset exit"

