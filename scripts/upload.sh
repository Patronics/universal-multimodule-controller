cd GamesteerRC
echo "currently incomplete, extend by adding the '--port' option for uf2 upload when debugger not present"
arduino-cli upload --fqbn rp2040:rp2040:generic_rp2350:variantchip=RP2350B,flash=16777216_8388608,freq=240,usbstack=tinyusb --build-path ../build
