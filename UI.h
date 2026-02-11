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
} menuItem;

void printStructWithLenAsHex(void* ptr, size_t length);
void printBytesAsHex(const char* data, int length);

#endif