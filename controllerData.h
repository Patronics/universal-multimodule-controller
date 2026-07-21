#include "ChannelManager.h"


#ifndef CONTROLLER_DATA_H
#define CONTROLLER_DATA_H


///TODO: use tuh_descriptor_get_hid_report https://sourcevu.sysprogs.com/rp2040/lib/tinyusb/files/src/host/usbh.h#tok1113
///to get full HID report to dynamically populate controller data


inline const USBGamepadLayoutDefinition GAMEPAD_TYPE_SONY_DS4 = {
  .name="Dualshock 4",
  .vid=0x054c,
  .pid=0x09cc,
  .reportLength = 64,
  .analogInputCount = 6,
  .analogInputReportOffsets = {1, 2, 3, 4, 8, 9},
  .analogInputNames = {"LX", "LY", "RX", "RY", "LT", "RT"},
  .digitalInputCount = 10,
  .digitalInputReportByteOffsets = {5   , 5   ,  5    , 5    , 6   , 6   , 6    , 6    , 6   , 6     },
  .digitalInputReportBitMask =     {0x10, 0x20,  0x40 , 0x80 , 0x01, 0x02, 0x10 , 0x20 , 0x40, 0x80  },
  .digitalInputNames =             {"Sq", "X" ,  "Cir", "Tri", "L1", "R1", "Shr", "Opt", "L3", "R3"  }
};

inline const USBGamepadLayoutDefinition GAMEPAD_TYPE_SONY_DS5 = {
  .name="Dualsense 5",
  .vid=0x054c,
  .pid=0x0ce6,
  .reportLength = 64,
  .analogInputCount = 6,
  .analogInputReportOffsets = {1, 2, 3, 4, 5, 6},
  .analogInputNames = {"LX", "LY", "RX", "RY", "LT", "RT"},
  .digitalInputCount = 0,
  .digitalInputReportByteOffsets = {0}, //value 0 properly initializes empty array with zeros
  .digitalInputReportBitMask = {0},
  .digitalInputNames = {"N/A"}
};

inline const USBGamepadLayoutDefinition GAMEPAD_TYPE_8BITDO_PRO2 = {
  .name="8bitdo Pro2",
  .vid=0x2dc8,
  .pid=0x6006,
  .reportLength = 11,
  .analogInputCount = 6,
  .analogInputReportOffsets = {2, 3, 4, 5, 6, 7},
  .analogInputNames = {"LX", "LY", "RX", "RY","RT", "LT"},
  .digitalInputCount = 0,
  .digitalInputReportByteOffsets = {0},
  .digitalInputReportBitMask = {0},
  .digitalInputNames = {"N/A"}
};

inline const USBGamepadLayoutDefinition END_MARKER_INVALID_GAMEPAD = {
  .name="Invalid",
  .vid=0x0000,
  .pid=0x0000,
  .reportLength = 0
};

inline const USBGamepadLayoutDefinition KNOWN_GAMEPAD_LAYOUTS[] = {GAMEPAD_TYPE_SONY_DS4, GAMEPAD_TYPE_SONY_DS5, GAMEPAD_TYPE_8BITDO_PRO2, END_MARKER_INVALID_GAMEPAD};

const USBGamepadLayoutDefinition *checkForKnownGamepadLayout(uint16_t vid, uint16_t pid);

#endif