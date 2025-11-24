#include "ChannelManager.h"


#ifndef CONTROLLER_DATA_H
#define CONTROLLER_DATA_H

inline const USBGamepadLayoutDefinition GAMEPAD_TYPE_SONY_DS4 = {
  .name="Dualshock 4",
  .vid=0x054c,
  .pid=0x09cc,
  .reportLength = 64,
  .analogInputCount = 6,
  .analogInputReportOffsets = {1, 2, 3, 4, 8, 9},
  .analogInputNames = {"LY", "LX", "RY", "RX", "LT", "RT"},
  .digitalInputCount = 0,
  .digitalInputReportByteOffsets = {0}, //value 0 properly initializes empty array with zeros
  .digitalInputReportBitMask = {0},
  .digitalInputNames = {"N/A"}
};

inline const USBGamepadLayoutDefinition GAMEPAD_TYPE_SONY_DS5 = {
  .name="Dualsense 5",
  .vid=0x054c,
  .pid=0x0ce6,
  .reportLength = 64,
  .analogInputCount = 6,
  .analogInputReportOffsets = {1, 2, 3, 4, 5, 6},
  .analogInputNames = {"LY", "LX", "RY", "RX", "LT", "RT"},
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
  .analogInputNames = {"LY", "LX", "RY", "RX","RT", "LT"},
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