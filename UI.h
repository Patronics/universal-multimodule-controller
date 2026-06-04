#include <Arduino.h>
#include <cstddef>
#include <stdint.h>

#ifndef UI_H
#define UI_H

const int DISPLAY_PIXEL_WIDTH = 128;
const int DISPLAY_PIXEL_HEIGHT = 64;


const int CHAR_WIDTH = 4;
const int CHAR_HEIGHT = 6;
const int DISPLAY_STR_BUFFER_SIZE = DISPLAY_PIXEL_WIDTH/CHAR_WIDTH;

const int MENU_ITEM_LABEL_SIZE = (DISPLAY_STR_BUFFER_SIZE/2)+1-2;  //calculated as half of the display width, -2 chars of spacing, +1 char for terminating null
const int MENU_ITEM_COUNT = 8; //number of menu items, adjust as needed


enum NavButton {
  NO_BUTTON_PRESSED,  //value corresponds to 0, so falsy
  OK_BUTTON,          //value 1, etc.
  BACK_BUTTON,
  UP_BUTTON,
  DOWN_BUTTON,
  LEFT_BUTTON,
  RIGHT_BUTTON,
  SYS_BUTTON,
  MDL_BUTTON
};

//a function pointer for handling a given menu item
typedef void (*MenuItemHandlerPtr)(int index, NavButton btnPressed);

//TODO: make menuitem struct that holds name, indexnum, and function to call for interactions to that menu. Populate it in setup, then iterate through for redrawMenu()
typedef struct {
  char label[MENU_ITEM_LABEL_SIZE];
  MenuItemHandlerPtr buttonHandler;
  int index;  //index number in menu
  int update_period_cycles; //if the menu should be updated periodically, update every one in how many cycles? Zero to only update on button press.
} menuItem;

void printStructWithLenAsHex(void* ptr, size_t length);
void printBytesAsHex(const char* data, int length);

void printBytesAsHex(const char* data, int length);

////////Debug Logging level handling:

constexpr uint32_t DEBUG_BUTTONS    = 1u << 0;
//NOTE: DEBUG_USB and DEBUG_USB_REPORT prints are invoked in tinyusb ISRs, which may cause reentrancy issues
//      to improve reliability, DEBUG_USB and DEBUG_USB_REPORT should be disabled when not required
constexpr uint32_t DEBUG_USB         = 1u << 1;
constexpr uint32_t DEBUG_USB_REPORT  = 1u << 2;
constexpr uint32_t DEBUG_SAVELOAD    = 1u << 3;
constexpr uint32_t DEBUG_LOG         = 1u << 4;
constexpr uint32_t DEBUG_WARN        = 1u << 5;
constexpr uint32_t DEBUG_ERROR       = 1u << 6;
constexpr uint32_t DEBUG_CORE        = 1u << 7;
constexpr uint32_t DEBUG_FS          = 1u << 8;
constexpr uint32_t DEBUG_MULTIMODULE = 1u << 9;
constexpr uint32_t DEBUG_TRANSMIT    = 1u << 10;
constexpr uint32_t DEBUG_TIME        = 1u << 11;

//mostly just to satisfy compiler, DEBUG_FLAGS should be set in the main .ino file
#ifndef DEBUG_FLAGS
  #define DEBUG_FLAGS (DEBUG_WARN|DEBUG_ERROR)
#endif
// constexpr value based on macro so we can use if constexpr for compile-time optimizations
inline constexpr uint32_t debug_flags = DEBUG_FLAGS;

////debugging loging

////SerialDebug (equivalent to Serial.print())
//usage: SerialDebug<DEBUG_LOG>("Serial Debugging enabled");
// basic string overload
template<uint32_t Mask>
inline void SerialDebug(const char *s) {
    if constexpr ((debug_flags & Mask) != 0) Serial.print(s);
}
template<uint32_t Mask>
inline void SerialDebug(char v) {
    if constexpr ((debug_flags & Mask) != 0) Serial.print(v);
}

// numeric overloads
template<uint32_t Mask>
inline void SerialDebug(int32_t v, int base = 10) {
    if constexpr ((debug_flags & Mask) != 0) Serial.print(v, base);
}
template<uint32_t Mask>
inline void SerialDebug(uint32_t v, int base = 10) {
    if constexpr ((debug_flags & Mask) != 0) Serial.print(v, base);
}
template<uint32_t Mask>
inline void SerialDebug(uint64_t v, int base = 10) {
    if constexpr ((debug_flags & Mask) != 0) Serial.print(v, base);
}
template<uint32_t Mask>
inline void SerialDebug(float v, int digits = 2) {
    if constexpr ((debug_flags & Mask) != 0) Serial.print(v, digits);
}

//generic for most other types
template<uint32_t Mask, typename T>
inline void SerialDebug(const T &v) {
    if constexpr ((debug_flags & Mask) != 0) {
        Serial.print(v);
    }
}


// println variants
template<uint32_t Mask>
inline void SerialDebugln(const char *s) {
    if constexpr ((debug_flags & Mask) != 0) Serial.println(s);
}
template<uint32_t Mask>
inline void SerialDebugln(char c) {
    if constexpr ((debug_flags & Mask) != 0) Serial.println(c);
}
//numeric
template<uint32_t Mask>
inline void SerialDebugln(int32_t v, int base = 10) {
    if constexpr ((debug_flags & Mask) != 0) Serial.println(v, base);
}
template<uint32_t Mask>
inline void SerialDebugln(uint32_t v, int base = 10) {
    if constexpr ((debug_flags & Mask) != 0) Serial.println(v, base);
}
template<uint32_t Mask>
inline void SerialDebugln(uint64_t v, int base = 10) {
    if constexpr ((debug_flags & Mask) != 0) Serial.println(v, base);
}
template<uint32_t Mask>
inline void SerialDebugln(float v, int digits = 2) {
    if constexpr ((debug_flags & Mask) != 0) Serial.println(v, digits);
}

//generic for most other types
template<uint32_t Mask, typename T>
inline void SerialDebugln(const T &v) {
    if constexpr ((debug_flags & Mask) != 0) {
        Serial.print(v);
        Serial.println();
    }
}

//just a newline
template<uint32_t Mask>
inline void SerialDebugln() {
    if constexpr ((debug_flags & Mask) != 0) Serial.println();
}

template<uint32_t Mask>
inline constexpr bool debug_level() {
    return (debug_flags & Mask) != 0;
}
//usage: if (debug_level<DEBUG_LOG>()){//do something;}


template<uint32_t Mask, typename... Args>
inline void SerialDebugf(const char *fmt, Args&&... args) {
    if constexpr ((debug_flags & Mask) != 0) {
        Serial.printf(fmt, std::forward<Args>(args)...);
    }
}
//equivalent to printf

#endif