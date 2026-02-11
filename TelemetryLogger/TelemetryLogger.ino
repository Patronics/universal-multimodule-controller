/*
TelemetryLogger.ino
Written By Patrick Leiser
Runs on Raspberry Pi Pico or similar RP2040 family boards
This script reads telemetry data from a 4-in-one multimodule.
*/

/*note: in arduino IDE, select:
  - Board "Raspberry Pi Pico > Generic RP2350" (from Earle Philhower)
  - Chip Variant "RP2350B"
  - Flash Size: 16MB
  - CPU Speed: 240MHz
*/


#include "MultiModule.h"
#include "ChannelManager.h"
#include "UI.h"
#include "controllerData.h"

// USBHost is defined in usbh_helper.h
#include "usbh_helper.h"

#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
#include <Bounce2.h>

//UART constants to configure:
//adjust as needed for other platforms or Serial Ports
#define SerialModule Serial1
//comment out the following defines if on a non-RP2040 platform where UART pins are fixed
#define SERIAL_MODULE_RX_PIN 13
#define SERIAL_MODULE_TX_PIN 16
#define SERIAL_MODULE_INVERT_RX

const unsigned long SERIAL_MODULE_BAUD_RATE = 100000;
const int SERIAL_MODULE_BUFFER_SIZE = 128;


unsigned long lastTelemetryMillis = 0;
unsigned long lastLoopMillis = 0;
unsigned long lastTxMillis = 0;

const unsigned long msBetweenTxUpdates = 7;

//Select active USB port, either 'A' or 'C'
char active_usb_port = 'C';

OutputChannelDescriptor outChannels[MAX_CHANNELS]; //declare 16 item OutputChannelDescriptor array
InputDescriptorPool inChannelsPool;


//setup i2c display
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

#define LED_PIN 33

//UI buttons
#define OK_BUTTON_PIN 28
#define BACK_BUTTON_PIN 32
#define UP_BUTTON_PIN 27
#define DOWN_BUTTON_PIN 29
#define LEFT_BUTTON_PIN 26
#define RIGHT_BUTTON_PIN 31
#define SYS_BUTTON_PIN 25
#define MDL_BUTTON_PIN 30

const uint16_t BUTTON_DEBOUNCE_INTERVAL = 5;
#define BUTTON_INPUT_TYPE INPUT_PULLUP
#define BUTTON_PRESSED_STATE LOW

Bounce2::Button okButton;
Bounce2::Button backButton;
Bounce2::Button upButton;
Bounce2::Button downButton;
Bounce2::Button leftButton;
Bounce2::Button rightButton;
Bounce2::Button sysButton;
Bounce2::Button mdlButton;

//for picking a menu
int selectedMenu = 0;
//the currently open menu, or -1 for none
int currentMenu = -1;

int menuSubpageIndex = 0;  //for arbitrary use by sub-menu logic, should be reset to zero on exit from submenu


menuItem menuItems[MENU_ITEM_COUNT];

USBInputDeviceDescriptor USBDeviceDescriptors[MAX_USB_DEVICE_DESCRIPTORS];
uint8_t numberOfDeviceDescriptors = 0;

//data structures for status and output
char capturedData[MAX_TELEM_DATA_BYTES + 1]; // Pre-allocated buffer, +1 for null-terminator
MultiModuleStatus moduleStatus;

MultiProtocolStream streamOut;
uint8_t streamOutAdditionalProtocolDataLen = 0;

uint8_t currentActiveProtocol = 0;
uint8_t currentActiveSubProtocol = 0;

bool transmitActive = false;
bool haveTelemetry = false;

void unimplementedMenuItemHandler(int index, NavButton btnPressed);



void setup() {

  pinMode(LED_PIN,OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  //bounce2 setup
  okButton.attach(OK_BUTTON_PIN, BUTTON_INPUT_TYPE);
  backButton.attach(BACK_BUTTON_PIN, BUTTON_INPUT_TYPE);
  upButton.attach(UP_BUTTON_PIN, BUTTON_INPUT_TYPE);
  downButton.attach(DOWN_BUTTON_PIN, BUTTON_INPUT_TYPE);
  leftButton.attach(LEFT_BUTTON_PIN, BUTTON_INPUT_TYPE);
  rightButton.attach(RIGHT_BUTTON_PIN, BUTTON_INPUT_TYPE);
  sysButton.attach(SYS_BUTTON_PIN, BUTTON_INPUT_TYPE);
  mdlButton.attach(MDL_BUTTON_PIN, BUTTON_INPUT_TYPE);

  //debounce interval
  okButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  backButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  upButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  downButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  leftButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  rightButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  sysButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  mdlButton.interval(BUTTON_DEBOUNCE_INTERVAL);

  okButton.setPressedState(BUTTON_PRESSED_STATE);
  backButton.setPressedState(BUTTON_PRESSED_STATE);
  upButton.setPressedState(BUTTON_PRESSED_STATE);
  downButton.setPressedState(BUTTON_PRESSED_STATE);
  leftButton.setPressedState(BUTTON_PRESSED_STATE);
  rightButton.setPressedState(BUTTON_PRESSED_STATE);
  sysButton.setPressedState(BUTTON_PRESSED_STATE);
  mdlButton.setPressedState(BUTTON_PRESSED_STATE);

  //set i2c pins for display
  Wire.setSDA(4);
  Wire.setSCL(5);
  u8g2.begin();
  u8g2.setFont(u8g2_font_tom_thumb_4x6_mf);

  Serial.begin(115200); //init USB serial

  //initialize SerialModule on GPIO 16 and 17 (pins 21 and 22), at 100kbaud, even parity, 2 stop bits
  #ifdef SERIAL_MODULE_RX_PIN
    SerialModule.setRX(SERIAL_MODULE_RX_PIN);
  #endif
  #ifdef SERIAL_MODULE_TX_PIN
    SerialModule.setTX(SERIAL_MODULE_TX_PIN);
  #endif
  #ifdef SERIAL_MODULE_INVERT_RX
    SerialModule.setInvertRX(true);
  #endif
  SerialModule.setFIFOSize(SERIAL_MODULE_BUFFER_SIZE);
  SerialModule.begin(SERIAL_MODULE_BAUD_RATE, SERIAL_8E2);

  //initialize stream values
  //start setting up output data structures
  streamOut.header.reserved_bits=0b010101;
  streamOut.header.is_failsafe = 0;
  setProtocolMode(&streamOut, 28, 0); //default protocol, 28=AFHDS2A, 0=PWM_IBUS
  streamOut.rx_num_power_type.rxNum = 0;
  streamOut.rx_num_power_type.power = 0; //high power
  streamOut.option_protocol = -128; //unknown purpose, matching example from transmitter for now
  streamOut.extended_protocol.telemetry_Invert = 1;

 /* while (!Serial) {
    ; // wait for USB serial port to connect
  }*/
  /*setChannelValue(&streamOut, 0, 2047);
  setChannelValue(&streamOut, 1, 0);
  setChannelValue(&streamOut, 2, 1);
  setChannelValue(&streamOut, 3, 0);
  setChannelValue(&streamOut, 4, 3);
  setChannelValue(&streamOut, 5, 0);
  setChannelValue(&streamOut, 6, 7);
  setChannelValue(&streamOut, 7, 0);
  setChannelValue(&streamOut, 8, 15);
  setChannelValue(&streamOut, 9, 0);
  setChannelValue(&streamOut, 10, 31);
  setChannelValue(&streamOut, 11, 0);
  setChannelValue(&streamOut, 12, 63);
  setChannelValue(&streamOut, 13, 0);
  setChannelValue(&streamOut, 14, 127);
  setChannelValue(&streamOut, 15, 0);*/
  Serial.println("start telemetry log.");

  initDefaultInputDescriptors(&inChannelsPool);
  initOutputAndDefaultInputChannelDescriptors(outChannels, &inChannelsPool, MAX_CHANNELS);
  


  Serial.print("output struct:");
  printStructWithLenAsHex(&streamOut, (sizeof(streamOut)-(9-streamOutAdditionalProtocolDataLen)));


  setupMenuLayout();
  redrawMenu(0, -1);
  digitalWrite(LED_PIN, LOW);
}

//core 1 is dedicated to USB host actions
//------------- Core1 -------------//
void setup1() {
  // configure pio-usb: defined in usbh_helper.h
  //Select active USB port, either 'A' or 'C'
  disable_usb();
  delay(1000);
  rp2040_configure_pio_usb(active_usb_port);
  // run host stack on controller (rhport) 1
  // Note: For rp2040 pico-pio-usb, calling USBHost.begin() on core1 will have most of the
  // host bit-banging processing works done in core1 to free up core0 for other works
  USBHost.begin(1);
}

void loop1() {
  USBHost.task();
}

//core zero runs all non-usb tasks
void loop() {
  // Update each button's state
  okButton.update();
  backButton.update();
  upButton.update();
  downButton.update();
  leftButton.update();
  rightButton.update();
  sysButton.update();
  mdlButton.update();

  NavButton currentNavButton = NO_BUTTON_PRESSED;
  if (okButton.pressed()) {
    currentNavButton = OK_BUTTON;
  }
  if (backButton.pressed()) {
    currentNavButton = BACK_BUTTON;
  }
  if (upButton.pressed()) {
    currentNavButton = UP_BUTTON;
  }
  if (downButton.pressed()) {
    currentNavButton = DOWN_BUTTON;
  }
  if (leftButton.pressed()) {
    currentNavButton = LEFT_BUTTON;
  }
  if (rightButton.pressed()) {
    currentNavButton = RIGHT_BUTTON;
  }
  if (sysButton.pressed()) {
    currentNavButton = SYS_BUTTON;
  }
  if (mdlButton.pressed()) {
    currentNavButton = MDL_BUTTON;
  }
  if(currentNavButton){   //NO_BUTTON_PRESSED is falsy
    handleNavButton(currentNavButton);
  }

  getTelemetry();
  /*if(Serial.available()){ //commands from computer

  }*/
  if(transmitActive && millis() - lastTxMillis >= msBetweenTxUpdates){
    lastTxMillis = millis();
    updateChannelValues(&streamOut);
    transmit(&streamOut, streamOutAdditionalProtocolDataLen);
  }

  //Serial.print("loop time: ");
  //Serial.println(millis()-lastLoopMillis);
  lastLoopMillis = millis();
}

static int clamp(int v, int lo, int hi){
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void updateChannelValues(MultiProtocolStream* s){
  for(int i=0; i<MAX_CHANNELS; i++){
    OutputChannelDescriptor* outChannel = &outChannels[i];
    InputChannelDescriptor* inChannel = outChannel->inputChannelDescriptor;
    int value = inChannel->getLatestInputData(inChannel->context, inChannel->id);
    //scale values as needed
    if(inChannel->minRange != outChannel->minRange || inChannel->maxRange != outChannel->maxRange){
      int inMin = inChannel->minRange;
      int inMax = inChannel->maxRange;
      int outMin = outChannel->minRange;
      int outMax = outChannel->maxRange;

      // clamp input to its expected range
      value = clamp(value, inMin, inMax);
      // avoid division by zero if input range is empty
      int inSpan = inMax - inMin;
      if (inSpan == 0) {
        // no span: map directly to outMin (or midpoint)
        value = outMin;
      } else {
        // perform integer linear mapping: out = outMin + (value - inMin) * outSpan / inSpan
        int outSpan = outMax - outMin;
        value = outMin + (int)((long)(value - inMin) * outSpan / inSpan);
      }
    }
    //Serial.print("setting value:");
    //Serial.print(value);
    setChannelValue(s, i, value);
  }

}

void transmit(MultiProtocolStream* s, uint8_t aditional_bytes){
  uint8_t* byteArray = (uint8_t*)s;
  SerialModule.write(byteArray, (sizeof(MultiProtocolStream)-(9-aditional_bytes)));
  printStructWithLenAsHex(&streamOut, (sizeof(streamOut)-(9-streamOutAdditionalProtocolDataLen)));
}

void drawMenuItem(const char* label, int thisPageIndex, int selectedMenu, int currentMenu) {
  //starting coordinates and movement pattern for menu items
  const int xOffset[] = {0, 64};
  const int yOffsetStart = 25;
  const int ySpacing = CHAR_HEIGHT;

  u8g2.setCursor(xOffset[thisPageIndex % 2], (yOffsetStart + (thisPageIndex/2)*ySpacing));
  if (thisPageIndex == currentMenu){
    u8g2.setDrawColor(0);  //highlight currently active menu
  } else {
    u8g2.setDrawColor(1);
  }
  if (thisPageIndex == selectedMenu) {
    u8g2.print("> "); // Add a marker for the selected item
  } else {
    u8g2.print("  "); // No marker for unselected items
  }
  u8g2.print(label); // Print the label of the menu item
  u8g2.setDrawColor(1);
};

void redrawMenu(int selectedMenuItem, int activeMenuItem) {
  for(int menuNumber = 0; menuNumber < MENU_ITEM_COUNT; menuNumber++){
    drawMenuItem(menuItems[menuNumber].label, menuNumber, selectedMenuItem, activeMenuItem);
  }
  u8g2.drawHLine(3, 44, DISPLAY_PIXEL_WIDTH);
}

void handleNavButton(NavButton btn){
  Serial.println(btn);
  if(currentMenu == -1){
    if(btn == UP_BUTTON){
      selectedMenu -= 2;
    } else if(btn == DOWN_BUTTON){
      selectedMenu += 2;
    } else if(btn == LEFT_BUTTON){
      selectedMenu -= 1;
    } else if(btn == RIGHT_BUTTON){
      selectedMenu += 1;
    } else if(btn == OK_BUTTON){
      currentMenu = selectedMenu;
    }
    if(selectedMenu < 0){
      selectedMenu = 0;
    }
  }
  //when menu is active, pass input to it;
  if (currentMenu >= 0) {
    menuItems[currentMenu].buttonHandler(currentMenu, btn);
  }
  //exit to main menu after allowing menu item function to perform cleanup as needed
  if (btn == BACK_BUTTON){
    currentMenu = -1;
  }
  
  redrawMenu(selectedMenu, currentMenu);
  u8g2.sendBuffer();
}

void clearMenuContents(){
    u8g2.setColorIndex(0); //erase
    u8g2.drawBox(0, 45, 128, 19);
    u8g2.setColorIndex(1); //erase
}

void setupMenuLayout(){
  int menuNumber = 0;
  strlcpy(menuItems[menuNumber].label, "Protocol", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = protocolSelectMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Sub-protocol", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = subProtocolSelectMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;
  strlcpy(menuItems[menuNumber].label, "TX Active", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = activateTxMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;
  strlcpy(menuItems[menuNumber].label, "Recv select", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = receiverSelectMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;
  // strlcpy(menuItems[menuNumber].label, "Optn-protocol", MENU_ITEM_LABEL_SIZE);
  // menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  // menuItems[menuNumber].index = menuNumber++;
  //menuNumber++;
  strlcpy(menuItems[menuNumber].label, "Channel Map", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = channelMapMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;
  strlcpy(menuItems[menuNumber].label, "Channel Range", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;
  strlcpy(menuItems[menuNumber].label, "Values", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;
  strlcpy(menuItems[menuNumber].label, "Failsafe Value", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuNumber++;
}


// ------- menu item handlers ------
//TODO: adjust menuItemHandlers to use a context pointer instead of the global menuSubpageIndex for tracking more flexible context
void unimplementedMenuItemHandler(int index, NavButton btnPressed){
  if (btnPressed == BACK_BUTTON){ //cleanup
    clearMenuContents();
    return;
  }
  u8g2.setCursor(0, 50);
  u8g2.print("unimplemented menu item: ");
  u8g2.print(index);
  u8g2.setCursor(0,56);
  u8g2.print(menuItems[index].label);
    u8g2.setCursor(0,62);
  u8g2.print("button pressed: ");
  u8g2.print(btnPressed);
}

void activateTxMenuItemHandler(int index, NavButton btnPressed){
  bool updated=false;
  if(btnPressed == LEFT_BUTTON){
    transmitActive = true;
    updated=true;
  } else if(btnPressed == RIGHT_BUTTON){
    transmitActive = false;
    updated=true;
  }
  if(updated || btnPressed == OK_BUTTON){
    u8g2.setCursor(0,50);
    if(transmitActive){
      u8g2.print("TX active  ");
    } else {
      u8g2.print("TX inactive");
    }
    u8g2.setCursor(0,56);
    u8g2.print("left: activate");
    u8g2.setCursor(DISPLAY_PIXEL_WIDTH/2,56);
    u8g2.print("right: disable");
    u8g2.setCursor(0,62);
    u8g2.print("Press back to return to menu");
  } else if (btnPressed == BACK_BUTTON){
    clearMenuContents();
  }
}

void protocolSelectMenuItemHandler(int index, NavButton btnPressed){
  bool updated=false;
  if (btnPressed == LEFT_BUTTON){
    updated=true;
    setProtocolMode(&streamOut,moduleStatus.prev_protocol, 0);
  }
  else if (btnPressed == RIGHT_BUTTON){
    updated=true;
    setProtocolMode(&streamOut,moduleStatus.next_protocol, 0);
  }
  if(updated || btnPressed == OK_BUTTON){
    u8g2.setCursor(0,50);
    u8g2.setDrawColor(0); //hightlight currently selected value
    u8g2.print("Current: ");
    u8g2PrintStrWithMaxLength( moduleStatus.protocol_name, sizeof(moduleStatus.protocol_name));
    u8g2.setDrawColor(1);
    u8g2.setCursor(0, 56);
    u8g2.print("Left: ");
    u8g2.print(moduleStatus.prev_protocol);
    u8g2.setCursor(DISPLAY_PIXEL_WIDTH/2, 56);
    u8g2.print("Right: ");
    u8g2.print(moduleStatus.next_protocol);
    if(!transmitActive){
      u8g2.setCursor(0,63);
      u8g2.print("tip: enable tx to see live data");
    }
  } else if (btnPressed == BACK_BUTTON){
    clearMenuContents();
  }
}

void subProtocolSelectMenuItemHandler(int index, NavButton btnPressed){
  bool updated=false;
  if (btnPressed == LEFT_BUTTON){
    updated=true;
    if(currentActiveSubProtocol > 0){
      setProtocolMode(&streamOut, currentActiveProtocol,currentActiveSubProtocol-1);
    }
  }
  else if (btnPressed == RIGHT_BUTTON){
    updated=true;
    if(currentActiveSubProtocol < moduleStatus.option_text_and_num_sub_protocols.parts.num_sub_protocols){
      setProtocolMode(&streamOut, currentActiveProtocol,currentActiveSubProtocol+1);
    }
  }
  if(updated || btnPressed == OK_BUTTON){
    u8g2.setCursor(0,50);
    u8g2.setDrawColor(0); //hightlight currently selected value
    u8g2.print("Current: ");
    u8g2PrintStrWithMaxLength( moduleStatus.sub_protocol_name, sizeof(moduleStatus.sub_protocol_name));
    u8g2.setDrawColor(1);
    u8g2.setCursor(0, 56);
    u8g2.print("Left: ");
    if(currentActiveSubProtocol > 0){
      u8g2.print(currentActiveSubProtocol-1);
      u8g2.print("   ");
    } else {
      u8g2.print("none");
    }
    u8g2.setCursor(DISPLAY_PIXEL_WIDTH/2, 56);
    u8g2.print("Right: ");
    if(currentActiveSubProtocol < moduleStatus.option_text_and_num_sub_protocols.parts.num_sub_protocols){
      u8g2.print(currentActiveSubProtocol+1);
      u8g2.print("   ");
    } else {
      u8g2.print("none");
    }
    if(!transmitActive){
      u8g2.setCursor(0,63);
      u8g2.print("tip: enable tx to see live data");
    }
  } else if (btnPressed == BACK_BUTTON){
    clearMenuContents();
  }
}

void receiverSelectMenuItemHandler(int index, NavButton btnPressed){
  bool updated = false;
  if (btnPressed == UP_BUTTON){
    updated = true;
    if(streamOut.rx_num_power_type.rxNum < 15){
      streamOut.rx_num_power_type.rxNum += 1;
    }
  } else if (btnPressed == DOWN_BUTTON){
    updated = true;
    if(streamOut.rx_num_power_type.rxNum > 0){
      streamOut.rx_num_power_type.rxNum -= 1;
    }
  } else if (btnPressed == LEFT_BUTTON){
    updated = true;
    streamOut.sub_protocol_flags.bindBit = ~streamOut.sub_protocol_flags.bindBit;
  } else if (btnPressed == RIGHT_BUTTON){
    updated = true;
    streamOut.sub_protocol_flags.autoBindBit = ~streamOut.sub_protocol_flags.autoBindBit;
  }
  if (updated || btnPressed == OK_BUTTON){
    u8g2.setCursor(0,50);
    u8g2.print("receiver number:");
    u8g2.setDrawColor(0);
    u8g2.print(" ");
    u8g2.print((int)streamOut.rx_num_power_type.rxNum, DEC);
    u8g2.print(" ");
    u8g2.setDrawColor(1);
    u8g2.setCursor(0, 56);
    u8g2.print("up/down: cycle receiver number");
    u8g2.setCursor(0, 62);
    u8g2.print("left: tgl ");
    if(streamOut.sub_protocol_flags.bindBit){
      u8g2.setDrawColor(0);
    }
    u8g2.print("bind");
    u8g2.setDrawColor(1);
    u8g2.print(", right: ");
    if(streamOut.sub_protocol_flags.autoBindBit){
      u8g2.setDrawColor(0);
    }
    u8g2.print("autobind");
    u8g2.setDrawColor(1);
  }
  else if (btnPressed == BACK_BUTTON){
    clearMenuContents();
  }
}

void channelMapMenuItemHandler(int index, NavButton btnPressed){
  bool updated = false;
  int channelInputIndex = Pool_FindIndexByIdAndType(&inChannelsPool, outChannels[menuSubpageIndex].inputChannelDescriptor->id, outChannels[menuSubpageIndex].inputChannelDescriptor->inputFunctionType);
  if (btnPressed == RIGHT_BUTTON){  //left/right changes mapped input for selected output
    updated = true;
    int nextIndex = Pool_FindNextUsedIndex(&inChannelsPool, channelInputIndex+1);
    if(nextIndex != -1){
      outChannels[menuSubpageIndex].inputChannelDescriptor = &(inChannelsPool.items[nextIndex]);
    }
  }
  else if (btnPressed == LEFT_BUTTON){
    updated = true;
        updated = true;
    int nextIndex = Pool_FindNextUsedIndex(&inChannelsPool, channelInputIndex-1);
    if(nextIndex != -1){
      outChannels[menuSubpageIndex].inputChannelDescriptor = &(inChannelsPool.items[nextIndex]);
    }
  }
  else if (btnPressed == DOWN_BUTTON){  //up/down changes selected channel
    updated = true;
    menuSubpageIndex -= 1;
    if (menuSubpageIndex<0){
      menuSubpageIndex = 0;
    }
  }
  else if (btnPressed == UP_BUTTON){
    updated = true;
    menuSubpageIndex += 1;
    if (menuSubpageIndex>=MAX_CHANNELS){
      menuSubpageIndex = MAX_CHANNELS-1;
    }
  }
  //note: currently menu renderers are not called during NO_BUTTON_PRESSED states, so the following logic would never run. If revised to run intermittently, this allows smoother refresh
  /*else if (btnPressed == NO_BUTTON_PRESSED){  //no complete refresh needed, but live value may have changed, so redraw that portion
    InputChannelDescriptor* currentInputDescriptor = outChannels[menuSubpageIndex].inputChannelDescriptor;
    u8g2.setCursor(0,56);
    u8g2.print("value: '");
    u8g2.print(currentInputDescriptor->getLatestInputData(currentInputDescriptor->context, currentInputDescriptor->id));
    u8g2.print("' from:    ");
  }*/
  if (updated || btnPressed == OK_BUTTON){
    InputChannelDescriptor* currentInputDescriptor = outChannels[menuSubpageIndex].inputChannelDescriptor;
    u8g2.setCursor(0,50);
    u8g2.print("output ch");
    u8g2.setDrawColor(0);
    u8g2.print(" ");
    u8g2.print(menuSubpageIndex, DEC);
    u8g2.print(" ");
    u8g2.setDrawColor(1);
    u8g2.print(" \"");
    u8g2.print(outChannels[menuSubpageIndex].name);
    u8g2.print("\"                ");
    u8g2.setCursor(0,56);
    u8g2.print("value: '");
    u8g2.print(currentInputDescriptor->getLatestInputData(currentInputDescriptor->context, currentInputDescriptor->id));
    u8g2.print("' from:    ");
    u8g2.setCursor(0,62);
    u8g2.print("input ch");
    u8g2.setDrawColor(0);
    u8g2.print(" ");
    u8g2.print(currentInputDescriptor->inputFunctionType, DEC);
    u8g2.print("_");
    u8g2.print(currentInputDescriptor->id, DEC);
    u8g2.print(" ");
    u8g2.setDrawColor(1);
    u8g2.print(" \"");
    u8g2.print(currentInputDescriptor->name);
    u8g2.print("\"                ");

  }
  else if (btnPressed == BACK_BUTTON){
    menuSubpageIndex=0;
    clearMenuContents();
  }
}

/*note that protocol and subProtocol names are inconsistient in documentation, and thus some of the struct names may be confusing.
In the multi-module docs,
protocolNum is sometimes referred to as "subProtocol" but other times as "protocol"
and subprotocolNum is sometimes referred to as "type" but other times as "subProtocol"
*/
void setProtocolMode(MultiProtocolStream *s, uint8_t protocolNum, uint8_t subprotocolNum){
  currentActiveProtocol = protocolNum;
  currentActiveSubProtocol = subprotocolNum;
  setProtocolModeBits(s, protocolNum, subprotocolNum);
}

int getTelemetry(){
  //if starting program and no telem received
  u8g2.setCursor(0,5);
  if(millis() < 15000 && lastTelemetryMillis == 0){
    u8g2.print("waiting for telemetry...");
    u8g2.sendBuffer();
    haveTelemetry=false;
  } else if (haveTelemetry==true && millis() - lastTelemetryMillis > 15000) {
    u8g2.print("no telemetry available...");
    u8g2.sendBuffer();
    haveTelemetry=false;
  }
  if (SerialModule.available() >= 4){
    //look for start of data header
    if (SerialModule.read() == 'M'){
      if(SerialModule.read() == 'P'){
        haveTelemetry = true;
        Serial.println("found valid header!");
        char type = SerialModule.read();
        char length = SerialModule.read();
        // Convert length byte to an integer for data capture
        uint8_t dataLength = (uint8_t)length;
        Serial.print("type:");
        Serial.println((int)type);
        Serial.print("length:");
        Serial.println(dataLength);
        if (dataLength <= 0 || dataLength > MAX_TELEM_DATA_BYTES) {
          Serial.println("Invalid data length!");
          return -1; // return error status
        }

        /*u8g2.setCursor(55, 55);     //log time between telemetry updates
        u8g2.print(millis()-lastTelemetryMillis);
        u8g2.print("   ");*/
        lastTelemetryMillis = millis();
        delay(5); //give data time to populate
        if (SerialModule.available() < dataLength) {
          Serial.println("Not enough data available!"); //note: if this occurs often, increase the delay
          Serial.print("expected ");
          Serial.print(dataLength);
          Serial.print(" bytes but only ");
          Serial.print(SerialModule.available());
          Serial.println(" available.");
          return -2;
        }
        
        SerialModule.readBytes(capturedData, dataLength);
        capturedData[dataLength] = '\0'; // Null-terminate the string
        Serial.print("Captured Data in Hex: ");
        printBytesAsHex(capturedData, dataLength);
        if (type == MULTI_MODULE_STATUS_TYPE && dataLength >= sizeof(MultiModuleStatus)){
          memcpy(&moduleStatus, capturedData, sizeof(MultiModuleStatus));
          Serial.println("MultiModule Status data found");
          printMultiModuleStatus(moduleStatus);
        } else if (type == FLYSKY_AFHDS2_TELEM_STATUS_TYPE){
          Serial.println("Flysky AFHDS2 telemetry data found"); //Flysky AFHDS2 telemetry type 0xAA
          Serial.print("RSSI: ");
          Serial.println((int)capturedData[0]);
          u8g2.drawStr(84, 5, "RSSI:");
          u8g2.setCursor(104, 5);
          u8g2.print((int)capturedData[0]);
          //TODO: parse the rest of this telemetry
        } else {
          Serial.print("unknown telemetry type");
        }
        //update i2c display
        u8g2.sendBuffer();
      }
    }
  }

  //check remaining serial data in buffer:
  int bytesInBuffer = SerialModule.available();
  if(bytesInBuffer > (SERIAL_MODULE_BUFFER_SIZE/2)){
    Serial.print("WARN: SerialModule Buf half full to ");
    Serial.println(bytesInBuffer);
  }
  return 0;
}


void printMultiModuleStatus(MultiModuleStatus status){
  //----  Status flags ----
  Serial.println("Flags:");
  Serial.print("  input signal detected: ");
  Serial.println(status.flags.bits.input_signal_detected);
  Serial.print("  serial mode enabled: ");
  Serial.println(status.flags.bits.serial_mode_enabled);
  Serial.print("  protocol valid: ");
  Serial.println(status.flags.bits.protocol_is_valid);
  Serial.print("  module in binding mode: ");
  Serial.println(status.flags.bits.module_is_in_binding_mode);
  Serial.print("  module waits for bind event: ");
  Serial.println(status.flags.bits.module_waits_for_bind_event);
  Serial.print("  curent protocol supports failsafe: ");
  Serial.println(status.flags.bits.protocol_supports_failsafe);
  Serial.print("  curent protocol supports disable channel mapping: ");
  Serial.println(status.flags.bits.protocol_supports_disable_channel_mapping);
  Serial.print("  data buffer almost full: ");
  Serial.println(status.flags.bits.data_buffer_almost_full);
  //---- Version ----
  Serial.print("Version: ");
  Serial.print((int)status.major);
  Serial.print(".");
  Serial.print((int)status.minor);
  Serial.print(".");
  Serial.print((int)status.revision);
  Serial.print(".");
  Serial.println((int)status.patchlevel);
  Serial.print("Channel Order: ");
  Serial.print(channelNames[status.channel_order.channels.ch1]);
  Serial.print(channelNames[status.channel_order.channels.ch2]);
  Serial.print(channelNames[status.channel_order.channels.ch3]);
  Serial.println(channelNames[status.channel_order.channels.ch4]);
  // ---- parse channel order byte: CH4|CH3|CH2|CH1 with CHx value A=0,E=1,T=2,R=3  ----
  Serial.print("Protocol Name: ");
  serialPrintStringWithMaxLength(status.protocol_name, sizeof(status.protocol_name));
  u8g2.setCursor(0,5);
  u8g2.print("protocol:   ");
  u8g2PrintStrWithMaxLength(status.protocol_name, sizeof(status.protocol_name));

  Serial.print("\nSub-Protocol Name: ");
  serialPrintStringWithMaxLength(status.sub_protocol_name, sizeof(status.sub_protocol_name));
  u8g2.print("          ");
  u8g2.setCursor(0,12);
  u8g2.print("subprotocol:");
  u8g2PrintStrWithMaxLength(status.sub_protocol_name, sizeof(status.sub_protocol_name));
  // ---- Option text and number of sub protocols ----
  Serial.print("\nOption text type: ");  //todo: evaluate option text
  Serial.println(status.option_text_and_num_sub_protocols.parts.option_text);
  Serial.print("number of sub-protocols: ");
  Serial.println(status.option_text_and_num_sub_protocols.parts.num_sub_protocols);
  Serial.print("Prev, Next protocol #");
  Serial.print(status.prev_protocol);
  Serial.print(",");
  Serial.println(status.next_protocol);
}

//print strings that may not be null-terminated if they reach max length
void serialPrintStringWithMaxLength(const char* str, int length){
  int actualLength = 0;
  for(int i=0; i<length; i++){
    if (str[i] == '\0') {
      break; // Stop if a null byte is encountered
    }
    actualLength++;
  }
  Serial.write(str, actualLength);
}

//uses u8g2.drawStr with explicitly passed coordinates
void u8g2DrawStrWithMaxLength(int x, int y, const char* str, const int length){
  if (length >= DISPLAY_STR_BUFFER_SIZE) {
    u8g2.setCursor(x,y);
    u8g2.print("Err:len ");
    u8g2.print(length);
    u8g2.print(" exceeds BUF_SIZE");
    return;
  }
  char buf[DISPLAY_STR_BUFFER_SIZE];
  strlcpy(buf, str, length+1);
  u8g2.drawStr(x,y,buf);
}

//uses u8g2.print, pads excess allocated space with whitespace
void u8g2PrintStrWithMaxLength(const char* str, const int length){
  if (length >= DISPLAY_STR_BUFFER_SIZE) {
    u8g2.print("Err:len ");
    u8g2.print(length);
    u8g2.print(" exceeds BUF_SIZE");
    return;
  }
  //populate buffer
  char buf[DISPLAY_STR_BUFFER_SIZE];
  strlcpy(buf, str, length+1);
  int strLen = strlen(buf);
  // If the string is shorter than the specified length, pad it with spaces
  if (strLen < length) {
      memset(buf + strLen, ' ', length - strLen); // Fill with spaces
      buf[length] = '\0'; // Null-terminate the string
  }
  u8g2.print(buf);
}

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+
extern "C"
{

//NOTE: tuh_hid_mount_cb does not seem to be called when the USB device is connected before the host starts.
//      with glow-clock derived proto-board, unplug USB device before starting host
//TODO: provide USB power switching capability in dedicated hardware implementation, then adjust usbh_helper PIN_5V_EN define accordingly


// Invoked when device with hid interface is mounted
// Report descriptor is also available for use.
// tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
// descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
// it will be skipped therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
  (void) desc_report;
  (void) desc_len;
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  Serial.printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  Serial.printf("VID = %04x, PID = %04x\r\n", vid, pid);

  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  int thisDeviceIndex = getFirstFreeUSBInputDescriptorIndex(USBDeviceDescriptors);

  if(thisDeviceIndex >= 0){
    numberOfDeviceDescriptors++;
    USBDeviceDescriptors[thisDeviceIndex].hidInterfaceType = itf_protocol; //may be overriden by further device idenficiation logic
    USBDeviceDescriptors[thisDeviceIndex].vid = vid;
    USBDeviceDescriptors[thisDeviceIndex].pid = pid;
    USBDeviceDescriptors[thisDeviceIndex].dev_addr = dev_addr;
    USBDeviceDescriptors[thisDeviceIndex].instance = instance;
    USBDeviceDescriptors[thisDeviceIndex].deviceNum = thisDeviceIndex;
    snprintf(USBDeviceDescriptors[thisDeviceIndex].name, INPUT_NAME_LEN, "usb dev #(%d)", thisDeviceIndex);
  } else {
    Serial.println("Error: max number of device descriptors exceeded!");
    Serial.println("returning early from USB setup, may result in undefined behavior");
    return;
  }

  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    Serial.printf("HID Keyboard\r\n");
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }
    //init keyboard number input channel:
    AllocateUSBKeyboardNumberInputChannel(&inChannelsPool, 0, "USB KB number", &USBDeviceDescriptors[thisDeviceIndex]);

  } else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
    Serial.printf("HID Mouse\r\n");
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }
  } else if(const USBGamepadLayoutDefinition *thisControllerLayout = checkForKnownGamepadLayout(vid, pid); thisControllerLayout){
    USBDeviceDescriptors[thisDeviceIndex].hidInterfaceType = USB_DESCRIPTOR_PROTOCOL_GAMEPAD;
    USBDeviceDescriptors[thisDeviceIndex].layoutDef = thisControllerLayout;
    Serial.print("found known gamepad: ");
    Serial.println(thisControllerLayout->name);
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      Serial.printf("Error: cannot request to receive report\r\n");
    }
    AllocateUSBGamepadStickChannels(&inChannelsPool, &USBDeviceDescriptors[thisDeviceIndex]);
  } else {
    Serial.print("unknown HID device:");
    Serial.println(itf_protocol);
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  USBInputDeviceDescriptor* descriptor = findUSBDescriptorByDevAddrAndInstance(USBDeviceDescriptors, dev_addr, instance);
  if(descriptor != NULL){
    descriptor -> vid = 0; //mark this descriptor as unused
    releaseUSBInputChannels(&inChannelsPool, descriptor);
  } else {
    Serial.print("Warning: unmounted device does not appear to have descriptor, check for erronious logic");
  }
  //TODO: free context for inputs if allocated
  Serial.printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

void handle_keyboard_key(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_t const *original_report) {
  USBInputDeviceDescriptor *descriptor = findUSBDescriptorByDevAddrAndInstance(USBDeviceDescriptors, dev_addr,instance);
  USBKeyboardContextType *context = (USBKeyboardContextType *)descriptor->inputChannels[0]->context;
  // only remap if not empty report i.e key released
  for (uint8_t i = 0; i < 6; i++) {
    if (original_report->keycode[i] != 0) {
      if(original_report->keycode[i] >= 30 && original_report->keycode[i] <=39){ //number keys on keyboard
        Serial.print("found key!");
        if(original_report->keycode[i] == 39){ //0 on keyboard
          context->latestValue = 0;
        } else {
          context->latestValue = (original_report->keycode[i] - 29); //shift by 29 to properly align numerical values
        }
      }
      Serial.print(original_report->keycode[i]);
      break;
    }
  }
}

//report len always expected to be 64 bytes
void handle_gamepad_input(USBInputDeviceDescriptor* thisDevice, uint8_t const *original_report, uint16_t len) {
  if(len > HID_REPORT_BUFSIZE){
    Serial.print("ERROR: invalid report length, truncating");
    len = HID_REPORT_BUFSIZE;
  }
  if (!thisDevice || !original_report){
    Serial.print("ERROR: device or original_report pointer is null");
    return;
  };
  memcpy(thisDevice->latest_report, original_report, len);
}


// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
  USBInputDeviceDescriptor* thisDevice = findUSBDescriptorByDevAddrAndInstance(USBDeviceDescriptors, dev_addr, instance);
  if (thisDevice -> hidInterfaceType == USB_DESCRIPTOR_PROTOCOL_KEYBOARD){
    if (len != 8) {
    Serial.printf("report len = %u NOT 8, not a keyboard!\r\n", len);
    printBytesAsHex((char *)report, len);
    } else {
      //hid_keyboard_report_t remapped_report;
      handle_keyboard_key(dev_addr, instance, (hid_keyboard_report_t const *) report);
    }
  } else if (thisDevice -> hidInterfaceType == USB_DESCRIPTOR_PROTOCOL_GAMEPAD){
    Serial.print("report:");
    printBytesAsHex((char *)report, len);
    if(len!=thisDevice->layoutDef->reportLength){
      Serial.printf("report len = %u NOT %u, not matching configured gamepad!\r\n", len, thisDevice->layoutDef->reportLength);
    } else {
      handle_gamepad_input(thisDevice, report, len);
    }
  }


  // continue to request to receive report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    Serial.printf("Error: cannot request to receive report\r\n");
  }
}

} //end extern "C" //end of tinyusb host callbacks
