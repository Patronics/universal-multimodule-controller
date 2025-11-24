#include "controllerData.h"
#include "ChannelManager.h"


const USBGamepadLayoutDefinition *checkForKnownGamepadLayout(uint16_t vid, uint16_t pid){
  const USBGamepadLayoutDefinition *currentPtr = KNOWN_GAMEPAD_LAYOUTS;
  while(1){
    if(currentPtr->vid == vid && currentPtr->pid == pid){
      return currentPtr;
    } else if (currentPtr->vid == 0 && currentPtr->pid==0){ //end of known gamepad list marker
      return NULL;
    }
    currentPtr++;
  }
}