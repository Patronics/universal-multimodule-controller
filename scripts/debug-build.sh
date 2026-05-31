cd GamesteerRC
arduino-cli compile --fqbn rp2040:rp2040:generic_rp2350:variantchip=RP2350B,flash=16777216_8388608,freq=240,usbstack=tinyusb,opt=Debug,exceptions=Enabled,uploadmethod=picoprobe_cmsis_dap --build-path ../build
