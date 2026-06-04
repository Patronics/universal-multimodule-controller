/*
TelemetryLogger.ino
Written By Patrick Leiser
Runs on Raspberry Pi Pico or similar RP2040 family boards
This script reads telemetry data from a 4-in-one multimodule.
*/

/*note: in arduino IDE, select:
  - Board "Raspberry Pi Pico > Generic RP2350" (from Earle Philhower)
  - Flash Size: 16MB (Sketch 8MB, FS 8MB)
  - CPU Speed: 240MHz (Overclock)
  - USB Stack: Adafruit TinyUSB
  - Chip Variant "RP2350B"
*/

//Note: DEBUG_FLAGS must be defined before including UI.h. See UI.h for list of valid flags
#define DEBUG_FLAGS (DEBUG_TRANSMIT|DEBUG_FS|DEBUG_SAVELOAD|DEBUG_LOG|DEBUG_WARN|DEBUG_ERROR|DEBUG_CORE)
//available flags: DEBUG_USB|DEBUG_USB_REPORT|DEBUG_TRANSMIT|DEBUG_MULTIMODULE|DEBUG_FS|DEBUG_SAVELOAD|DEBUG_LOG|DEBUG_WARN|DEBUG_ERROR|DEBUG_CORE

#include "MultiModule.h"
#include "ChannelManager.h"
#include "UI.h"
#include "controllerData.h"

// USBHost is defined in usbh_helper.h
#include "usbh_helper.h"

#include <VFS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

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

//currently unused, for saving persistient system-wide settings
struct persistent_settings {
  char enabled_port;
  uint8_t default_protocol_mode;
  uint8_t default_subprotocol_mode;
};


//Select active USB port, either 'A' or 'C', or 'Z' to prompt clean-up and shutdown of the ports
volatile char requested_usb_port = 'C';
char active_usb_port = requested_usb_port;

OutputChannelDescriptor outChannels[MAX_CHANNELS]; //declare 16 item OutputChannelDescriptor array
MixerChannelDescriptor mixerChannels[MAX_MIXERS];
InputDescriptorPool inChannelsPool;
InputChannelDescriptor* failsafeChannels[MAX_CHANNELS];


//#define USE_I2C_DISPLAY
#define USE_PRIMARY_SPI_DISPLAY
//#define USE_SW_2ND_SPI_DISPLAY
//#define USE_HW_2ND_SPI_DISPLAY

//setup i2c display (used in pre-release hardware, slower than SPI display interfaces)
#ifdef USE_I2C_DISPLAY
  //U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
  #define I2C_DISPLAY_SDA_PIN 4
  #define I2C_DISPLAY_SCL_PIN 5
#endif

//the primary intended display interface
//for PCB 1.0, the display's VCC pin connection to VBUS MUST be bypassed, instead connect display's VCC to 3V3
#ifdef USE_PRIMARY_SPI_DISPLAY
  #define USE_HW_SPI_DISPLAY
  #define SPI_DISPLAY_RST_PIN 5
  #define SPI_DISPLAY_DC_PIN 4
  #define SPI_DISPLAY_CS_PIN 1
  #define SPI_DISPLAY_CLK_PIN 2
  #define SPI_DISPLAY_TX_PIN 3
  U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, SPI_DISPLAY_CS_PIN,SPI_DISPLAY_DC_PIN, SPI_DISPLAY_RST_PIN);

#endif

//convenient pinout for using display with GPIO header pins 3v3, gnd, and GPIO 46, 44, 42, 40, 38
//software-spi only so limited to 300khz clock rate
#ifdef USE_SW_2ND_SPI_DISPLAY
  #define SPI_DISPLAY_RST_PIN 38
  #define SPI_DISPLAY_DC_PIN 40
  #define SPI_DISPLAY_CS_PIN 42
  #define SPI_DISPLAY_CLK_PIN 44
  #define SPI_DISPLAY_TX_PIN 46
  U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI u8g2(U8G2_R0, SPI_DISPLAY_CLK_PIN, SPI_DISPLAY_TX_PIN, SPI_DISPLAY_CS_PIN,SPI_DISPLAY_DC_PIN, SPI_DISPLAY_RST_PIN);
#endif

//use the gpio header pins with hardware SPI, improved performance over USE_SW_2ND_SPI_DISPLAY, but more complex pin layout
#ifdef USE_HW_2ND_SPI_DISPLAY
  #define USE_HW_SPI_DISPLAY
  #define SPI_DISPLAY_RST_PIN 46
  #define SPI_DISPLAY_DC_PIN 44
  #define SPI_DISPLAY_CS_PIN 37
  #define SPI_DISPLAY_CLK_PIN 34
  #define SPI_DISPLAY_TX_PIN 35
  U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, SPI_DISPLAY_CS_PIN,SPI_DISPLAY_DC_PIN, SPI_DISPLAY_RST_PIN);
#endif

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

const int CURRENT_MENU_NONE = -1;

int menuSubpageIndex = 0;  //for arbitrary use by sub-menu logic, should be reset to zero on exit from submenu
void *menuSubpageContext = NULL; //for arbitrary use for more advanced state in submenu logic, should be properly freed on exit of submenu
int menuUpdateCycleCount =0; //counter to update menu periodically if needed

menuItem sysMenu[MENU_ITEM_COUNT];
menuItem mdlMenu[MENU_ITEM_COUNT];

menuItem* menuItems = mdlMenu;


// USB Language ID: English
#define LANGUAGE_ID 0x0409

// CFG_TUH_DEVICE_MAX is defined by tusb_config header
usb_dev_info_t usb_raw_descriptors[CFG_TUH_DEVICE_MAX] = { 0 };

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
bool failsafeActive = false;
bool haveTelemetry = false;




void setup() {

  Serial.begin(115200); //init USB serial
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
  #ifdef USE_I2C_DISPLAY
    Wire.setSDA(I2C_DISPLAY_SDA_PIN);
    Wire.setSCL(I2C_DISPLAY_SCL_PIN);
  #endif
  //setup SPI pins for display
  #ifdef USE_HW_SPI_DISPLAY
    SPI.setRX(NOPIN);
    SPI.setTX(SPI_DISPLAY_TX_PIN);
    SPI.setSCK(SPI_DISPLAY_CLK_PIN);
    SPI.setCS(SPI_DISPLAY_CS_PIN);
    //u8g2.setBusClock(1000000); //1Mhz, fallback value for debugging
    u8g2.setBusClock(8000000); //8Mhz
  #endif
  //otherwise use software SPI display, not setup required:
  /*#ifdef USE_SW_2ND_SPI_DISPLAY
    bit-banged SPI does not support setBusClock configuration
    it runs at around 300kHz in my testing
  #endif*/
 
  u8g2.begin();
  u8g2.setFont(u8g2_font_tom_thumb_4x6_mf);

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

  /*while (!Serial) {
    ; // wait for USB serial port to connect
  }*/

  SerialDebug<DEBUG_LOG>("Serial Debugging enabled: log level ");
  SerialDebug<DEBUG_LOG>(debug_flags);
  SerialDebug<DEBUG_LOG>("\n");

  if(!LittleFS.begin()){
    SerialDebug<DEBUG_ERROR>("FS begin failed");
  } else {
    VFS.root(LittleFS);
    FSInfo fs_info;
    if (!LittleFS.info(fs_info)) {
      SerialDebug<DEBUG_WARN>("FS info() failed");
    } else {
      if(debug_level<DEBUG_FS>()){
        Serial.println("LittleFS info:");
        Serial.print("  totalBytes:     "); Serial.println((unsigned long long)fs_info.totalBytes);
        Serial.print("  usedBytes:      "); Serial.println((unsigned long long)fs_info.usedBytes);
        Serial.print("  blockSize:      "); Serial.println((size_t)fs_info.blockSize);
        Serial.print("  pageSize:       "); Serial.println((size_t)fs_info.pageSize);
        Serial.print("  maxOpenFiles:   "); Serial.println((size_t)fs_info.maxOpenFiles);
        Serial.print("  maxPathLength:  "); Serial.println((size_t)fs_info.maxPathLength);
      }
    }
  }

  Serial.println("starting.");

  //initDefaultInputDescriptors(&inChannelsPool);
  initOutputAndDefaultInputChannelDescriptors(outChannels, &inChannelsPool, mixerChannels, failsafeChannels);
  


  //Serial.print("output struct:");
  //printStructWithLenAsHex(&streamOut, (sizeof(streamOut)-(9-streamOutAdditionalProtocolDataLen)));


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
  SerialDebugln<DEBUG_CORE>("Core1 setup to run TinyUSB host with pio-usb");
  rp2040_configure_pio_usb(active_usb_port);
  // run host stack on controller (rhport) 1
  // Note: For rp2040 pico-pio-usb, calling USBHost.begin() on core1 will have most of the
  // host bit-banging processing works done in core1 to free up core0 for other works
  USBHost.begin(1);
}

void loop1() {
  //valid requested USB ports are A, C, and Z (disabled).
  if (requested_usb_port != active_usb_port){
    SerialDebugln<DEBUG_LOG>("Updating active USB port!");
    disable_usb();
    delay(1000);
    USBHost.task();
    USBHost.task();
    USBHost.task();
    if(requested_usb_port == 'A' || requested_usb_port=='C'){
      change_active_usb_port(requested_usb_port);
    }
    active_usb_port = requested_usb_port;

  } else if(active_usb_port == 'A' || active_usb_port == 'C') {
    USBHost.task();
  }
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
    handleNavButton(BACK_BUTTON); //force exit active menu
    menuItems = sysMenu;
    selectedMenu = 0;
    redrawMenu(selectedMenu, CURRENT_MENU_NONE);
    currentNavButton = NO_BUTTON_PRESSED;
  }
  if (mdlButton.pressed()) {
    handleNavButton(BACK_BUTTON); //force exit active menu
    menuItems = mdlMenu;
    selectedMenu = 0;
    redrawMenu(selectedMenu, CURRENT_MENU_NONE);
    currentNavButton = NO_BUTTON_PRESSED;
  }
  if(currentNavButton){   //NO_BUTTON_PRESSED is falsy
    menuUpdateCycleCount = 0;
    handleNavButton(currentNavButton);

  } else if(currentMenu >= 0 && menuItems[currentMenu].update_period_cycles){  //if a submenu active and the submenu requests periodic updates
    menuUpdateCycleCount++;
    if (menuUpdateCycleCount >= menuItems[currentMenu].update_period_cycles){
      handleNavButton(NO_BUTTON_PRESSED);
    }
  }

  getTelemetry();
  /*if(Serial.available()){ //commands from computer

  }*/
  if(transmitActive && millis() - lastTxMillis >= msBetweenTxUpdates){
    lastTxMillis = millis();
    updateChannelValues(&streamOut);
    transmit(&streamOut, streamOutAdditionalProtocolDataLen);
  }

  SerialDebug<DEBUG_TIME>("loop time: ");
  SerialDebugln<DEBUG_TIME>(millis()-lastLoopMillis);
  lastLoopMillis = millis();
}

static int clamp(int v, int lo, int hi){
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int getOutputChannelValue(int channelIndex){
  OutputChannelDescriptor* outChannel = &outChannels[channelIndex];
  InputChannelDescriptor* inChannel = outChannel->inputChannelDescriptor;
  int value = getLatestInputData(inChannel, channelIndex);
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
  return value;
}

int getFailsafeChannelValue(int channelIndex){
  InputChannelDescriptor* thisFailsafe = failsafeChannels[channelIndex];
  int value = thisFailsafe->getLatestInputData(thisFailsafe->context, thisFailsafe->id, channelIndex);
  return value;
}

void updateChannelValues(MultiProtocolStream* s){
  for(int i=0; i<MAX_CHANNELS; i++){
    int value;
    if(failsafeActive){
      value = getFailsafeChannelValue(i);
    } else {
      value = getOutputChannelValue(i);
    }
    //Serial.print("setting value:");
    //Serial.print(value);
    setChannelValue(s, i, value);
  }
}

void updateFailsafeValues(MultiProtocolStream* s){
  for(int i=0; i<MAX_CHANNELS; i++){
    int value = getFailsafeChannelValue(i);
    setChannelValue(s, i, value);
  }
  s->header.is_failsafe = 1;
  transmit(s, streamOutAdditionalProtocolDataLen);
  s->header.is_failsafe = 0;
}

void transmit(MultiProtocolStream* s, uint8_t aditional_bytes){
  uint8_t* byteArray = (uint8_t*)s;
  SerialModule.write(byteArray, (sizeof(MultiProtocolStream)-(9-aditional_bytes)));
  if(debug_level<DEBUG_TRANSMIT>()){
    printStructWithLenAsHex(&streamOut, (sizeof(streamOut)-(9-streamOutAdditionalProtocolDataLen)));
  }
}

void drawMenuItem(const char* label, int thisPageIndex, int selectedMenu, int currentMenu) {
  //starting coordinates and movement pattern for menu items
  const int xOffset[] = {0, 64};
  const int yOffsetStart = 22;
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
  u8g2.setDrawColor(1);
  u8g2.drawHLine(0, 14, DISPLAY_PIXEL_WIDTH);  //line separating telemetry from menu
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 15, DISPLAY_PIXEL_WIDTH, 29); //draw blank box over menu area, region 15<y<44
  for(int menuNumber = 0; menuNumber < MENU_ITEM_COUNT; menuNumber++){
    drawMenuItem(menuItems[menuNumber].label, menuNumber, selectedMenuItem, activeMenuItem);
  }
  u8g2.setDrawColor(1);
  u8g2.drawHLine(0, 44, DISPLAY_PIXEL_WIDTH);
  u8g2.sendBuffer();
}

void drawCompactMenu(int headingMenuItem) {
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 15, DISPLAY_PIXEL_WIDTH, 30); //draw box over large menu area, region 15<y<45
  u8g2.setDrawColor(1);
  u8g2.drawHLine(0, 14, DISPLAY_PIXEL_WIDTH);  //line separating telemetry from menu
  u8g2.setCursor(0,22);
  u8g2.print(menuItems[headingMenuItem].label);
  u8g2.drawHLine(0, 25, DISPLAY_PIXEL_WIDTH);  //line separating menu heading from submenu
}

void handleNavButton(NavButton btn){
  SerialDebug<DEBUG_BUTTONS>(static_cast<uint32_t>(btn));
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
      drawCompactMenu(selectedMenu);
      currentMenu = selectedMenu;
    }
    if(selectedMenu < 0){
      selectedMenu = 0;
    }
    if(selectedMenu >= MENU_ITEM_COUNT){
      selectedMenu -= 2;
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
  if(currentMenu == -1){
    //draw full menu, and update it for current selection
    redrawMenu(selectedMenu, currentMenu);
  } else {
    //explicitly send buffer if not automatically doing so in redrawMenu()
    u8g2.sendBuffer();
  }
}


void clearMenuContents(){
    u8g2.setColorIndex(0); //erase
    u8g2.drawBox(0, 45, 128, 19);
    u8g2.setColorIndex(1); //end erase
}

void setupMenuLayout(){

  ////////model-menu items ////////
  int menuNumber = 0;
  menuItems = mdlMenu;

  strlcpy(menuItems[menuNumber].label, "Load/Save mdl", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = modelSaveLoadMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Protocol", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = protocolSelectMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 50; //infrequent updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Channel Map", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = channelMapMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 10; //frequent updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Sub-protocol", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = subProtocolSelectMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 50; //infrequent updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Mixes", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = mixerMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 10; //frequent updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Recv select", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = receiverSelectMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Set Failsafes", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = failsafeSnapshotMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 50; //infrequent updates required
  menuNumber++;


  strlcpy(menuItems[menuNumber].label, "Live Channels", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = liveChannelViewMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 10; //frequent updates required
  menuNumber++;

//////// system-menu items
  menuItems = sysMenu;
  menuNumber=0;

  strlcpy(menuItems[menuNumber].label, "TX Active", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = activateTxMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "USB Port", MENU_ITEM_LABEL_SIZE);
  menuItems[menuNumber].buttonHandler = usbPortSelectMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "Defaults", MENU_ITEM_LABEL_SIZE);  //reserved for future use, populate memory slot 2
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "", MENU_ITEM_LABEL_SIZE);  //reserved for future use, populate memory slot 3
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "", MENU_ITEM_LABEL_SIZE);  //reserved for future use, populate memory slot 4
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "", MENU_ITEM_LABEL_SIZE);  //reserved for future use, populate memory slot 5
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "", MENU_ITEM_LABEL_SIZE);  //reserved for future use, populate memory slot 6
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  strlcpy(menuItems[menuNumber].label, "", MENU_ITEM_LABEL_SIZE);  //reserved for future use, populate memory slot 7
  menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  menuItems[menuNumber].index = menuNumber;
  menuItems[menuNumber].update_period_cycles = 0; //no updates required
  menuNumber++;

  // strlcpy(menuItems[menuNumber].label, "Optn-protocol", MENU_ITEM_LABEL_SIZE);
  // menuItems[menuNumber].buttonHandler = unimplementedMenuItemHandler;
  // menuItems[menuNumber].index = menuNumber++;
  //menuNumber++;


}


// ------- menu item handlers ------
//Menu items are permitted to draw anywhere within the region x>25 (so setCursor of 0, 32 for prints) (expanded from previous region of x>45).
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
  } else if(btnPressed == DOWN_BUTTON){
    failsafeActive = false;
    updated=true;
  } else if(btnPressed == UP_BUTTON){
    failsafeActive = true;
    updated=true;
  }
  if(updated || btnPressed == OK_BUTTON){
    u8g2.setCursor(0,35);
    if(transmitActive){
      u8g2.print("TX Active  ");
    } else {
      u8g2.print("TX Inactive");
    }
    u8g2.setCursor(60,35);
    if(failsafeActive){
      u8g2.print("Failsafe Active  ");
    } else {
      u8g2.print("Failsafe Inactive");
    }
    u8g2.setCursor(0,56);
    u8g2.print("left/right: toggle TX");
    u8g2.setCursor(0,62);
    u8g2.print("up/down: toggle failsafe");
  } else if (btnPressed == BACK_BUTTON){
    clearMenuContents();
  }
}


void usbPortSelectMenuItemHandler(int index, NavButton btnPressed){
  bool updated=false;
  if (btnPressed == LEFT_BUTTON){
    updated=true;
    requested_usb_port = 'A';
  } else if (btnPressed == RIGHT_BUTTON){
    updated=true;
    requested_usb_port = 'C';
  } else if (btnPressed == DOWN_BUTTON){
    updated=true;
    requested_usb_port = 'Z';
  } else if (btnPressed == BACK_BUTTON){
    clearMenuContents();
  }
  if(updated || btnPressed == OK_BUTTON){
    u8g2.setCursor(0,32);
    u8g2.setDrawColor(0); //hightlight currently selected value
    u8g2PrintPadding();
    u8g2.print("Current: ");
    u8g2.print(requested_usb_port);
    u8g2.setDrawColor(1);
    u8g2.setCursor(0, 56);
    u8g2.print("Left: USB A,");
    u8g2.setCursor(DISPLAY_PIXEL_WIDTH/2, 56);
    u8g2.print("Right: USB C,");
    u8g2.setCursor(0, 62);
    u8g2.print("Down: disable USB");
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
  else if (btnPressed == NO_BUTTON_PRESSED || btnPressed == OK_BUTTON){  //periodic update or ok button to request update
    updated=true;
  }
  if(updated){
    u8g2.setCursor(0,32);
    u8g2.setDrawColor(0); //hightlight currently selected value
    u8g2PrintPadding();
    u8g2.print("Current: ");
    u8g2PrintStrWithMaxLength( moduleStatus.protocol_name, sizeof(moduleStatus.protocol_name));
    u8g2.setDrawColor(1);
    u8g2.print("  (");
    u8g2.print(currentActiveProtocol);
    u8g2.print(") ");
    u8g2.setDrawColor(1);
    u8g2.setCursor(0, 42);
    u8g2.print("Left: (");
    u8g2.print(moduleStatus.prev_protocol);
    u8g2.print(") ");
    //TODO: optionally get name of adjacent protocols to show in addition to protocol numbers.
    u8g2.setCursor(0, 52);
    u8g2.print("Right: (");
    u8g2.print(moduleStatus.next_protocol);
    u8g2.print(") ");
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
  else if (btnPressed == NO_BUTTON_PRESSED || btnPressed == OK_BUTTON){  //periodic update or ok button to request update
    updated=true;
  }
  if(updated){
    u8g2.setCursor(0,50);
    u8g2.setDrawColor(0); //hightlight currently selected value
    u8g2.print("Current: ");
    u8g2PrintStrWithMaxLength( moduleStatus.sub_protocol_name, sizeof(moduleStatus.sub_protocol_name));
    u8g2.setDrawColor(1);
    u8g2.print("  (");
    u8g2.print(currentActiveSubProtocol);
    u8g2.print(") ");
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
  else if (btnPressed == NO_BUTTON_PRESSED){  //periodic refresh: no complete refresh needed, but live value may have changed, so redraw that portion
    InputChannelDescriptor* currentInputDescriptor = outChannels[menuSubpageIndex].inputChannelDescriptor;
    u8g2.setCursor(0, 40);
    drawLiveChannelValueBar(menuSubpageIndex, 100, "");
    u8g2.setCursor(0,56);
    u8g2.print("value: '");
    u8g2.print(getLatestInputData(currentInputDescriptor, menuSubpageIndex));
    u8g2.print("' from:    ");
  }
  if (updated || btnPressed == OK_BUTTON){
    InputChannelDescriptor* currentInputDescriptor = outChannels[menuSubpageIndex].inputChannelDescriptor;
    u8g2.setCursor(0,32);
    u8g2.print("output ch");
    u8g2.setDrawColor(0);
    u8g2.print(" ");
    u8g2.print(menuSubpageIndex, DEC);
    u8g2.print(" ");
    u8g2.setDrawColor(1);
    u8g2.print(" \"");
    u8g2.print(outChannels[menuSubpageIndex].name);
    u8g2.print("\"                ");
    u8g2.setCursor(0, 40);
    drawLiveChannelValueBar(menuSubpageIndex, 100, "");
    u8g2.setCursor(0,56);
    u8g2.print("value: '");
    u8g2.print(getLatestInputData(currentInputDescriptor, menuSubpageIndex));
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


void mixerMenuItemHandler(int index, NavButton btnPressed){
  bool updated = false;
  int valueShift = 0;
  mixerMenuItemHandlerContext* activeMixer;
  if (menuSubpageContext == NULL){ //when first opening menu
    menuSubpageContext = calloc(1, sizeof(mixerMenuItemHandlerContext));
    activeMixer = (mixerMenuItemHandlerContext *)menuSubpageContext; 
    menuSubpageIndex=0;
    activeMixer->currentlyEditingMixer = &mixerChannels[menuSubpageIndex];
    //TODO: populate mixerMenuItemHandlerContext to a given mixer item stored in mix 0
  }
  activeMixer = (mixerMenuItemHandlerContext *)menuSubpageContext; 

  if (btnPressed == LEFT_BUTTON){
    updated = true;
    valueShift = -1;
  } else if (btnPressed == RIGHT_BUTTON){
    updated = true;
    valueShift = 1;
  } else if (btnPressed == UP_BUTTON){
    updated=true;
    activeMixer->mixerActiveEditState = (mixerEditTargetState)((int)activeMixer->mixerActiveEditState - 1);
    if(activeMixer->mixerActiveEditState <= MIXER_EDIT_STATE_ENUM_UNDERFLOW){
      activeMixer->mixerActiveEditState = MIXER_EDIT_STATE_CHANGE_MIXER_ITEM;
    }
  } else if (btnPressed == DOWN_BUTTON){
    updated=true;
    activeMixer->mixerActiveEditState = (mixerEditTargetState)((int)activeMixer->mixerActiveEditState + 1);
    if(activeMixer->mixerActiveEditState >= MIXER_EDIT_STATE_ENUM_OVERFLOW){
      activeMixer->mixerActiveEditState = MIXER_EDIT_STATE_CHANGE_MIXER_ITEM;
    }
  } else if( btnPressed == OK_BUTTON){
    updated=true;
    activeMixer->mixerActiveEditState = MIXER_EDIT_STATE_CHANGE_MIXER_ITEM;
  } else if (btnPressed == BACK_BUTTON){
    free(menuSubpageContext);  //cleanup the context object
    menuSubpageIndex=0;
    menuSubpageContext=NULL;
    clearMenuContents();
    return;
  }
  if(valueShift){ //both -1 and 1 are truthy, handle left/right buttons
    switch (activeMixer->mixerActiveEditState){
      case MIXER_EDIT_STATE_CHANGE_MIXER_ITEM:
        menuSubpageIndex+=valueShift;
        menuSubpageIndex = clamp(menuSubpageIndex, 0, MAX_MIXERS-1); //clamp to valid mixer range
        activeMixer->currentlyEditingMixer = &mixerChannels[menuSubpageIndex];
        break;
      case MIXER_EDIT_STATE_OPERATION:
        activeMixer->currentlyEditingMixer->operation = (MixerCombineOperation)((int)activeMixer->currentlyEditingMixer->operation + valueShift);
        if (activeMixer->currentlyEditingMixer->operation >= MIXER_OP_ENUM_OVERFLOW){
          activeMixer->currentlyEditingMixer->operation = MIXER_OP_CH1_ONLY;
        } else if (activeMixer->currentlyEditingMixer->operation <= MIXER_OP_ENUM_UNDERFLOW){
          activeMixer->currentlyEditingMixer->operation = (MixerCombineOperation)((int)MIXER_OP_ENUM_OVERFLOW-1);
        }
        break;
      case MIXER_EDIT_STATE_A_SOURCE:
      case MIXER_EDIT_STATE_B_SOURCE:
        {
          InputChannelDescriptor** activeInputChannelDescriptor; //double pointer to refer to either inputChannel1Descriptor or inputChannel2Descriptor
          if(activeMixer->mixerActiveEditState == MIXER_EDIT_STATE_A_SOURCE){
            activeInputChannelDescriptor = &(activeMixer->currentlyEditingMixer->inputChannel1Descriptor);
          } else { //MIXER_EDIT_STATE_B_SOURCE
            activeInputChannelDescriptor = &(activeMixer->currentlyEditingMixer->inputChannel2Descriptor);
          }
          int channelInputIndex = Pool_FindIndexByIdAndType(&inChannelsPool, (*activeInputChannelDescriptor)->id, (*activeInputChannelDescriptor)->inputFunctionType);
          int nextChannelIndex;
          if(valueShift == 1){
            nextChannelIndex = Pool_FindNextUsedIndex(&inChannelsPool, channelInputIndex+1);
          } else {  //if valueShift == -1
            nextChannelIndex = Pool_FindPreviousUsedIndex(&inChannelsPool, channelInputIndex-1);
          }
          if(nextChannelIndex != -1){ //verify valid channel present
            InputChannelDescriptor** potentialNewChannelDescriptor;
            (*potentialNewChannelDescriptor) = &(inChannelsPool.items[nextChannelIndex]);
            //special case handling to avoid infinite mixer recursion: only allow reference to lower-numbered mixers
            if((*potentialNewChannelDescriptor)->inputFunctionType == INPUT_FUNCTION_MIXER && (*potentialNewChannelDescriptor)->id >= activeMixer->currentlyEditingMixer->id){
              if(valueShift == 1){
                channelInputIndex = Pool_FindIndexByIdAndType(&inChannelsPool, MAX_MIXERS-1, INPUT_FUNCTION_MIXER);
                nextChannelIndex = Pool_FindNextUsedIndex(&inChannelsPool, channelInputIndex+1);
              } else { //valueshift ==-1
                channelInputIndex = Pool_FindIndexByIdAndType(&inChannelsPool, activeMixer->currentlyEditingMixer->id, INPUT_FUNCTION_MIXER);
                nextChannelIndex = Pool_FindPreviousUsedIndex(&inChannelsPool, channelInputIndex-1);
              }
            }
            (*activeInputChannelDescriptor) = &(inChannelsPool.items[nextChannelIndex]);

          }
          break;
        }
      case MIXER_EDIT_STATE_A_SCALE:
      case MIXER_EDIT_STATE_B_SCALE:
        {
          int* activeMixerScale;
          if(activeMixer->mixerActiveEditState == MIXER_EDIT_STATE_A_SCALE){
            activeMixerScale = &activeMixer->currentlyEditingMixer->channel1Scale;
          } else {
            activeMixerScale = &activeMixer->currentlyEditingMixer->channel2Scale;
          }
          *activeMixerScale += valueShift;
        break;
      }
    case MIXER_EDIT_STATE_A_OFFSET:
    case MIXER_EDIT_STATE_B_OFFSET:
      {
        int* activeMixerOffset;
          if(activeMixer->mixerActiveEditState == MIXER_EDIT_STATE_A_OFFSET){
            activeMixerOffset = &activeMixer->currentlyEditingMixer->channel1Offset;
          } else {
            activeMixerOffset = &activeMixer->currentlyEditingMixer->channel2Offset;
          }
          *activeMixerOffset += valueShift;
        break;
      }
    case MIXER_EDIT_STATE_A_INVERT:
    case MIXER_EDIT_STATE_B_INVERT:
      {
        bool* activeMixerInvert;
        if(activeMixer->mixerActiveEditState == MIXER_EDIT_STATE_A_INVERT){
          activeMixerInvert = &activeMixer->currentlyEditingMixer->channel1Invert;
        } else {
          activeMixerInvert = &activeMixer->currentlyEditingMixer->channel2Invert;
        }
        *activeMixerInvert = !*activeMixerInvert;
      }
    }
  }
  if(updated){
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 26, 128, 38);
    u8g2.setDrawColor(1);
    u8g2.drawVLine(64, 25, 39);
    u8g2.setCursor(0,32);
    u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_CHANGE_MIXER_ITEM);
    //sample values for formatting
    u8g2PrintStrWithMaxLength(activeMixer->currentlyEditingMixer->name,7);
    //u8g2.print(" Mix 0:");
    u8g2.setCursor(100,32);
    u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_OPERATION);
    u8g2PrintPadding();
    u8g2.print("OP:");
    u8g2.print(MixerCombineOperationString[activeMixer->currentlyEditingMixer->operation]);
    u8g2.setDrawColor(1);
    u8g2.setCursor(30,32);
    u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_A_SOURCE);
    u8g2PrintPadding();
    u8g2.print("A:");
    if(activeMixer->currentlyEditingMixer->inputChannel1Descriptor == NULL){
      u8g2.print("NULL");
        u8g2.setCursor(0, 39);
        u8g2.print("Source not");
        u8g2.setCursor(0, 45);
        u8g2.print("present");
        u8g2.setCursor(0,52);
        u8g2.print("failsafe active");
    } else {
      u8g2.print(activeMixer->currentlyEditingMixer->inputChannel1Descriptor->inputFunctionType, DEC);
      u8g2.print("_");
      u8g2.print(activeMixer->currentlyEditingMixer->inputChannel1Descriptor->id, DEC);
      u8g2.setCursor(0, 39);
      u8g2.print(activeMixer->currentlyEditingMixer->inputChannel1Descriptor->name);
      //u8g2.print("\"Dualsense 5-LY\"");
      //u8g2.setCursor(64, 56);
      //u8g2.print(" L/R: Edit mode");
      u8g2.setCursor(0, 45);
      u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_A_SCALE);
      u8g2.print("Scale:");
      u8g2.print(activeMixer->currentlyEditingMixer->channel1Scale);
      u8g2.print(" ");
      u8g2.setCursor(0,51);
      u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_A_INVERT);
      u8g2.print("Invert:");
      u8g2.print(" ");
      if(activeMixer->currentlyEditingMixer->channel1Invert){
        u8g2.print("yes ");
      } else {
        u8g2.print("no ");
      }
      u8g2.setCursor(0,58);
      u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_A_OFFSET);
      u8g2.print("Offset:");
      u8g2.print(activeMixer->currentlyEditingMixer->channel1Offset);
      u8g2.print(" ");
    }
    if(activeMixer->currentlyEditingMixer->operation != MIXER_OP_CH1_ONLY){
      u8g2.setCursor(66,32);
      u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_B_SOURCE);
      u8g2PrintPadding();
      u8g2.print("B:");
      if(activeMixer->currentlyEditingMixer->inputChannel2Descriptor == NULL){
        u8g2.print("NULL");
        u8g2.setCursor(66, 39);
        u8g2.print("Source not");
        u8g2.setCursor(66, 45);
        u8g2.print("present");
        u8g2.setCursor(0,52);
        u8g2.print("failsafe active");
      } else {
        u8g2.print(activeMixer->currentlyEditingMixer->inputChannel2Descriptor->inputFunctionType, DEC);
        u8g2.print("_");
        u8g2.print(activeMixer->currentlyEditingMixer->inputChannel2Descriptor->id, DEC);
        u8g2.setCursor(66, 39);
        u8g2.print(activeMixer->currentlyEditingMixer->inputChannel2Descriptor->name);
        u8g2.setCursor(85, 45);
        u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_B_SCALE);
        u8g2.print("Scale:");
        u8g2.print(activeMixer->currentlyEditingMixer->channel2Scale);
        u8g2.print(" ");
        u8g2.setCursor(85,51);
        u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_B_INVERT);
        u8g2.print("Invert:");
        u8g2.print(" ");
        if(activeMixer->currentlyEditingMixer->channel2Invert){
          u8g2.print("yes ");
        } else {
          u8g2.print("no ");
        }
        u8g2.setCursor(85,58);
        u8g2.setDrawColor(activeMixer->mixerActiveEditState != MIXER_EDIT_STATE_B_OFFSET);
        u8g2.print("Offset:");
        u8g2.print(activeMixer->currentlyEditingMixer->channel2Offset);
        u8g2.print(" ");
      }
    }
  } //end if(updated)
  //update values in real-time even if no changes to menu
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 58, 128, 8);
  u8g2.setDrawColor(1);
  u8g2.setCursor(42,63);
  u8g2.print("Value: ");
  u8g2.print(getLatestInputData(activeMixer->currentlyEditingMixer->mixerResultDescriptor, -1));
  u8g2.setCursor(2,63);
  u8g2.print(evaluateSingleChannelMixerValue(activeMixer->currentlyEditingMixer, true));
  u8g2.setCursor(95,63);
  u8g2.print(evaluateSingleChannelMixerValue(activeMixer->currentlyEditingMixer, false));
}

void failsafeSnapshotMenuItemHandler(int index, NavButton btnPressed){
  bool updated=false;
  for(int i=0; i<MAX_CHANNELS && i<12; i++){
    if(i%3 == 0){
      u8g2.setCursor(0, 35+7*(i/3));
    }
    int failsafeValue = getFailsafeChannelValue(i);
    drawLiveChannelMultiBar(i, failsafeValue);
  }
  u8g2.setCursor(0, 63);
  u8g2.print("down: update failsafe values");
  if (btnPressed == DOWN_BUTTON){
    updated=true;
    for(int i=0; i<MAX_CHANNELS; i++){
      failsafeChannels[i]->configureChannelInput(failsafeChannels[i],getOutputChannelValue(i));
    }
    updateFailsafeValues(&streamOut);
  }
  if (btnPressed == BACK_BUTTON){
    menuSubpageIndex=0;
    clearMenuContents();
  }
}

void liveChannelViewMenuItemHandler(int index, NavButton btnPressed){
  //u8g2.setCursor(0,35); //implicit, set by the setCursor line below
  for(int i=0; i<MAX_CHANNELS; i++){
    if(i%3 == 0){
      u8g2.setCursor(0, 35+7*(i/3));
    }
    drawLiveChannelValueBar(i);
  }
  
  if(btnPressed == BACK_BUTTON){
    menuSubpageIndex=0;
    clearMenuContents();
  }
}

void modelSaveLoadMenuItemHandler(int index, NavButton btnPressed){
  bool updated=false;
  if (btnPressed == LEFT_BUTTON){
    updated=true;
    menuSubpageIndex -= 1;
    if(menuSubpageIndex < 0){
      menuSubpageIndex = 0;
    }
  } else if (btnPressed == RIGHT_BUTTON){
    updated=true;
    menuSubpageIndex += 1;
  } else if (btnPressed == UP_BUTTON){
    updated=true;
    saveModelToFileAtIndex(menuSubpageIndex);
  } else if (btnPressed == DOWN_BUTTON){
    updated=true;
    loadModelFromFileAtIndex(menuSubpageIndex);
  } else if( btnPressed == OK_BUTTON){
    updated=true;
  } else if (btnPressed == BACK_BUTTON){
    menuSubpageIndex=0;
    clearMenuContents();
  }
  if(updated){
    u8g2.setCursor(0,50);
    u8g2.print("Current: ");
    u8g2.print(menuSubpageIndex);
    u8g2.print(" ");
    u8g2.setDrawColor(0); //hightlight currently selected value
    u8g2.print(" ");
    u8g2.print(getBasicModelStringByIndex(menuSubpageIndex));
    u8g2.setDrawColor(1); //clear highlight
    u8g2.print("      ");
    u8g2.setCursor(0, 56);
    u8g2.print("Left/Right : select model");
    u8g2.setCursor(0, 62);
    u8g2.print("Down: Load");
    u8g2.setCursor(DISPLAY_PIXEL_WIDTH/2, 62);
    u8g2.print("Up: Save");
  }
}

// end of menu item handlers //

// ------ Model load/save routines ------- //
String getBasicModelStringByIndex(int index){
  if(!LittleFS.exists("/models/index/"+String(index)+"/model.json")){
    return "No model saved";
  }
  File modelFile = LittleFS.open("/models/index/"+String(index)+"/model.json", "r");
  JsonDocument modelDoc;
  deserializeJson(modelDoc, modelFile);
  modelFile.close();
  uint8_t protocol = modelDoc["protocol"];
  uint8_t subprotocol = modelDoc["subprotocol"];
  char port = (char)modelDoc["port"].as<unsigned char>();
  return ("prot: "+String(protocol)+":"+String(subprotocol)+"("+port+")");
}

void saveModelToFileAtIndex(int index){
  File newModelFile = LittleFS.open("/models/index/"+String(index)+"/model.json", "w");
  JsonDocument newModelDoc;
  newModelDoc["protocol"] = currentActiveProtocol;
  newModelDoc["subprotocol"] = currentActiveSubProtocol;
  newModelDoc["port"] = (unsigned char)active_usb_port;
  JsonArray channelsArray = newModelDoc["channels"].to<JsonArray>();
  for(int i=0; i<MAX_CHANNELS; i++){
    JsonDocument channelObj;
    InputChannelDescriptor *inputChannel = outChannels[i].inputChannelDescriptor;
    int channelTypeEnumVal = int(inputChannel->inputFunctionType);
    channelObj["type"]=channelTypeEnumVal;
    channelObj["name"] = inputChannel->name; //mostly used for usb device fuzzy matching
    channelObj["id"] = inputChannel->id;
    channelObj["minRange"] = outChannels[i].minRange;
    channelObj["maxRange"] = outChannels[i].maxRange;

    channelsArray[i]=channelObj;
  }
  JsonArray mixersArray = newModelDoc["mixers"].to<JsonArray>();
  for(int i=0; i<MAX_MIXERS; i++){
    JsonDocument mixerObj;
    mixerObj["id"]=mixerChannels[i].id;
    int operationTypeEnumVal = int(mixerChannels[i].operation);
    mixerObj["op"]= operationTypeEnumVal;
    mixerObj["name"]= mixerChannels[i].name;
    InputChannelDescriptor *inputChannel1 = mixerChannels[i].inputChannel1Descriptor;
    InputChannelDescriptor *inputChannel2 = mixerChannels[i].inputChannel2Descriptor;
    int channel1TypeEnumVal = int(inputChannel1->inputFunctionType);
    int channel2TypeEnumVal = int(inputChannel2->inputFunctionType);
    mixerObj["type1"]=channel1TypeEnumVal;
    mixerObj["type2"]=channel2TypeEnumVal;
    mixerObj["name1"] = inputChannel1->name;
    mixerObj["name2"] = inputChannel2->name;
    mixerObj["id1"] = inputChannel1->id;
    mixerObj["id2"] = inputChannel2->id;
    mixerObj["scale1"] = mixerChannels[i].channel1Scale;
    mixerObj["scale2"] = mixerChannels[i].channel2Scale;
    mixerObj["offset1"] = mixerChannels[i].channel1Offset;
    mixerObj["offset2"] = mixerChannels[i].channel2Offset;
    mixerObj["invert1"] = mixerChannels[i].channel1Invert;
    mixerObj["invert2"] = mixerChannels[i].channel2Invert;

    mixersArray[i]=mixerObj;
  }
  JsonArray failsafesArray = newModelDoc["failsafes"].to<JsonArray>();
  SerialDebugln<DEBUG_SAVELOAD>("saving failsafe values:");
  for(int i=0; i<MAX_CHANNELS; i++){
    SerialDebug<DEBUG_SAVELOAD>(failsafeChannels[i]->getLatestInputData(failsafeChannels[i]->context, i, i));
    SerialDebug<DEBUG_SAVELOAD>(" ");
    failsafesArray[i] = failsafeChannels[i]->getLatestInputData(failsafeChannels[i]->context, i, i);
  }
  if(debug_level<DEBUG_SAVELOAD>()){
    serializeJson(newModelDoc, Serial); //print json to USB Serial log
  }
  serializeJson(newModelDoc, newModelFile);
  newModelFile.close();
}

bool loadModelFromFileAtIndex(int index){
  if(!LittleFS.exists("/models/index/"+String(index)+"/model.json")){
    SerialDebug<DEBUG_SAVELOAD|DEBUG_FS>("no model file");
    return false;
  }
  File modelFile = LittleFS.open("/models/index/"+String(index)+"/model.json", "r");
  JsonDocument modelDoc;
  deserializeJson(modelDoc, modelFile);
  modelFile.close();
  //serializeJson(modelDoc, Serial);  //print json to USB Serial log
  uint8_t protocol = modelDoc["protocol"];
  SerialDebug<DEBUG_SAVELOAD>("loading protocol:");
  SerialDebugln<DEBUG_SAVELOAD>(protocol);
  uint8_t subprotocol = modelDoc["subprotocol"] | 0;
  SerialDebug<DEBUG_SAVELOAD>("loading subprotocol:");
  SerialDebug<DEBUG_SAVELOAD>(static_cast<uint32_t>(subprotocol));
  setProtocolMode(&streamOut, protocol, subprotocol);
  char port = (char)modelDoc["port"].as<unsigned char>() | 'A';
  SerialDebug<DEBUG_SAVELOAD>("loading usb port:");
  SerialDebugln<DEBUG_SAVELOAD>(port);
  requested_usb_port = port;
  SerialDebug<DEBUG_SAVELOAD>("loading channels");
  JsonArray channelsArray = modelDoc["channels"];
  if(debug_level<DEBUG_SAVELOAD>()){
    //serializeJson(channelsArray, Serial);
  }
  if(channelsArray){
    for(int i=0; i<MAX_CHANNELS; i++){
      JsonObject channelObj = channelsArray[i];
      SerialDebug<DEBUG_SAVELOAD>("\nloading channel: ");
      SerialDebugln<DEBUG_SAVELOAD>(i);
      if(debug_level<DEBUG_SAVELOAD>()){
        serializeJson(channelObj, Serial);
      }
      const char* channelName = channelObj["name"];
      SerialDebugln<DEBUG_SAVELOAD>(channelName);
      //optional todo: populate loaded name (currently just ch0, etc, as prepopulated)
      int channelTypeEnumVal = channelObj["type"];
      outChannels[i].minRange = channelObj["minRange"];
      outChannels[i].maxRange = channelObj["maxRange"];
      InputChannelDescriptor *matchingInputDescriptor = findInputDescriptorWithTypeNameAndId(&inChannelsPool, (InputFunctionType)channelTypeEnumVal, channelObj["name"], channelObj["id"]);
      if (matchingInputDescriptor == NULL){
          SerialDebug<DEBUG_SAVELOAD>("No matching channels (USB or id/type), skipping channel");
          continue;
      }
      outChannels[i].inputChannelDescriptor=matchingInputDescriptor;
    }
  } else {
    SerialDebug<DEBUG_SAVELOAD|DEBUG_WARN>("Warning, channels array invalid or not present");
  }
  SerialDebug<DEBUG_SAVELOAD>("\nloading mixers:\n");
  JsonArray mixersArray = modelDoc["mixers"];
  if(mixersArray){
    //for debugging, print to console
    if(debug_level<DEBUG_SAVELOAD>()){
      //serializeJson(mixersArray, Serial);
    }
    for(int i=0; i<MAX_MIXERS; i++){
      JsonObject mixerObj = mixersArray[i];
      SerialDebug<DEBUG_SAVELOAD>("\nloading mixer: ");
      SerialDebugln<DEBUG_SAVELOAD>(i);
      if(debug_level<DEBUG_SAVELOAD>()){
        serializeJson(mixerObj, Serial);
      }
      const char* mixerName = mixerObj["name"];
      SerialDebugln<DEBUG_SAVELOAD>(mixerName);
      //optional todo: populate loaded name (currently just mix 0, etc, as prepopulated)
      mixerChannels[i].id = mixerObj["id"];
      int operationTypeEnumVal = mixerObj["op"];
      mixerChannels[i].operation = (MixerCombineOperation)operationTypeEnumVal;
      InputChannelDescriptor *matchingInputDescriptor1 = findInputDescriptorWithTypeNameAndId(&inChannelsPool, (InputFunctionType)mixerObj["type1"], mixerObj["name1"], mixerObj["id1"]);
      InputChannelDescriptor *matchingInputDescriptor2 = findInputDescriptorWithTypeNameAndId(&inChannelsPool, (InputFunctionType)mixerObj["type2"], mixerObj["name2"], mixerObj["id2"]);
      mixerChannels[i].inputChannel1Descriptor = matchingInputDescriptor1;
      mixerChannels[i].inputChannel2Descriptor = matchingInputDescriptor2;
      mixerChannels[i].channel1Scale = mixerObj["scale1"];
      mixerChannels[i].channel2Scale = mixerObj["scale2"];
      mixerChannels[i].channel1Offset = mixerObj["offset1"];
      mixerChannels[i].channel2Offset = mixerObj["offset2"];
      mixerChannels[i].channel1Invert = mixerObj["invert1"];
      mixerChannels[i].channel2Invert = mixerObj["invert2"];
    }
  } else {
    SerialDebug<DEBUG_SAVELOAD|DEBUG_WARN>("Warning, mixers array invalid or not present");
  }
  SerialDebug<DEBUG_SAVELOAD>("\nloading failsafes:\n");
  JsonArray failsafesArray = modelDoc["failsafes"];
  if(failsafesArray){
    for(int i=0; i<MAX_CHANNELS; i++){
      SerialDebug<DEBUG_SAVELOAD>("\nloading failsafe ");
      SerialDebug<DEBUG_SAVELOAD>(i);
      SerialDebug<DEBUG_SAVELOAD>(": ");
      SerialDebug<DEBUG_SAVELOAD>((int)failsafesArray[i]);
      failsafeChannels[i]->configureChannelInput(failsafeChannels[i],failsafesArray[i]);
    }
    updateFailsafeValues(&streamOut);
  } else {
    SerialDebug<DEBUG_SAVELOAD|DEBUG_WARN>("Warning, failsafes array invalid or not present");
  }
  return true;
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
        SerialDebugln<DEBUG_MULTIMODULE>("found valid header!");
        char type = SerialModule.read();
        char length = SerialModule.read();
        // Convert length byte to an integer for data capture
        uint8_t dataLength = (uint8_t)length;
        SerialDebug<DEBUG_MULTIMODULE>("type:");
        SerialDebugln<DEBUG_MULTIMODULE>((int)type);
        SerialDebug<DEBUG_MULTIMODULE>("length:");
        SerialDebugln<DEBUG_MULTIMODULE>(dataLength);
        if (dataLength <= 0 || dataLength > MAX_TELEM_DATA_BYTES) {
          SerialDebugln<DEBUG_MULTIMODULE|DEBUG_WARN>("Invalid data length!");
          return -1; // return error status
        }

        /*u8g2.setCursor(55, 55);     //log time between telemetry updates
        u8g2.print(millis()-lastTelemetryMillis);
        u8g2.print("   ");*/
        lastTelemetryMillis = millis();
        delay(5); //give data time to populate
        if (SerialModule.available() < dataLength) {
          SerialDebugln<DEBUG_MULTIMODULE|DEBUG_WARN>("Not enough data available!"); //note: if this occurs often, increase the delay
          SerialDebug<DEBUG_MULTIMODULE|DEBUG_WARN>("expected ");
          SerialDebug<DEBUG_MULTIMODULE|DEBUG_WARN>(dataLength);
          SerialDebug<DEBUG_MULTIMODULE|DEBUG_WARN>(" bytes but only ");
          SerialDebug<DEBUG_MULTIMODULE|DEBUG_WARN>(SerialModule.available());
          SerialDebugln<DEBUG_MULTIMODULE|DEBUG_WARN>(" available.");
          return -2;
        }
        
        SerialModule.readBytes(capturedData, dataLength);
        capturedData[dataLength] = '\0'; // Null-terminate the string
        SerialDebug<DEBUG_MULTIMODULE>("Captured Data in Hex: ");
        if(debug_level<DEBUG_MULTIMODULE>()){
          printBytesAsHex(capturedData, dataLength);
        }
        if (type == MULTI_MODULE_STATUS_TYPE && dataLength >= sizeof(MultiModuleStatus)){
          memcpy(&moduleStatus, capturedData, sizeof(MultiModuleStatus));
          SerialDebugln<DEBUG_MULTIMODULE>("MultiModule Status data found");
          if(debug_level<DEBUG_MULTIMODULE>()){
            printMultiModuleStatus(moduleStatus);
          }
        } else if (type == FLYSKY_AFHDS2_TELEM_STATUS_TYPE){
          SerialDebugln<DEBUG_MULTIMODULE>("Flysky AFHDS2 telemetry data found"); //Flysky AFHDS2 telemetry type 0xAA
          SerialDebug<DEBUG_MULTIMODULE>("RSSI: ");
          SerialDebugln<DEBUG_MULTIMODULE>((int)capturedData[0]);
          u8g2.drawStr(84, 5, "RSSI:");
          u8g2.setCursor(104, 5);
          u8g2.print((int)capturedData[0]);
          //TODO: parse the rest of this telemetry
        } else {
          SerialDebugln<DEBUG_MULTIMODULE|DEBUG_LOG>("unknown telemetry type");
        }
        //update i2c display
        u8g2.sendBuffer();
      }
    }
  }

  //check remaining serial data in buffer:
  int bytesInBuffer = SerialModule.available();
  if(bytesInBuffer > (SERIAL_MODULE_BUFFER_SIZE/2)){
    SerialDebug<DEBUG_MULTIMODULE|DEBUG_WARN>("WARN: SerialModule Buf half full to ");
    SerialDebug<DEBUG_MULTIMODULE|DEBUG_WARN>(bytesInBuffer);
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

//print a 1 pixel wide by 1 char tall padding before/after a string (especially useful for cleaner text in inverted highlights)
void u8g2PrintPadding(){
  int8_t height = u8g2.getMaxCharHeight();
  int8_t x = u8g2.getCursorX();
  int8_t y = u8g2.getCursorY();
  int8_t initialDrawColor = u8g2.getDrawColor();
  u8g2.setDrawColor(!initialDrawColor);
  u8g2.drawVLine(x, y-(height-1), height);
  u8g2.setCursor(x+1, y);
  u8g2.setDrawColor(initialDrawColor);
}

void u8g2PrintBar(int value, int minValue, int maxValue, int maxWidth){
  int8_t height = u8g2.getMaxCharHeight();
  int8_t x = u8g2.getCursorX();
  int8_t y = u8g2.getCursorY();
  int8_t initialDrawColor = u8g2.getDrawColor();
  int barWidth = ((value - minValue) * (maxWidth-2))/(maxValue - minValue);
  //draw outer box
  u8g2.drawBox(x, y-height, maxWidth, height);
  u8g2.setDrawColor(!initialDrawColor);
  //erase inside of box
  u8g2.drawBox(x+1, y-height+1, maxWidth-2, height-2);
  u8g2.setDrawColor(initialDrawColor);
  //draw inner bar
  u8g2.drawBox(x+1, y-height+1, barWidth, height-2);
  u8g2.setCursor(x+maxWidth+1, y);
}

//draw a more complex 2-row bar, such as for showing both an original/default and a live value
void u8g2PrintMultiBar(int value1,int value2, int minValue, int maxValue, int maxWidth){
  int8_t height = u8g2.getMaxCharHeight();
  int8_t x = u8g2.getCursorX();
  int8_t y = u8g2.getCursorY();
  int8_t initialDrawColor = u8g2.getDrawColor();
  int bar1Width = ((value1 - minValue) * (maxWidth-2))/(maxValue - minValue);
  int bar2Width = ((value2 - minValue) * (maxWidth-2))/(maxValue - minValue);
  //draw outer box
  u8g2.drawBox(x, y-height, maxWidth, height);
  u8g2.setDrawColor(!initialDrawColor);
  //erase inside of box
  u8g2.drawBox(x+1, y-height+1, maxWidth-2, height-2);
  u8g2.setDrawColor(initialDrawColor);
  //draw bar1
  u8g2.drawBox(x+1, y-height+1, bar1Width, (height/2)-1);
  //draw bar2
  u8g2.drawBox(x+1, y-(height/2), bar2Width, (height/2)-1);
  u8g2.setCursor(x+maxWidth+1, y);


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

//overload to provide default value for width
void drawLiveChannelValueBar(int index){
  drawLiveChannelValueBar(index, 12);
}

void drawLiveChannelValueBar(int index, int width){
  u8g2.print("Ch ");
  u8g2.print((int)index);
  u8g2.print(":");
  if(index<10){
    u8g2.print(" ");
  }
  u8g2PrintBar(getOutputChannelValue(index),outChannels[index].minRange, outChannels[index].maxRange, width);
  u8g2.print(" ");
}

void drawLiveChannelValueBar(int index, int width, char* description){
  u8g2.print(description);
  u8g2PrintBar(getOutputChannelValue(index),outChannels[index].minRange, outChannels[index].maxRange, width);
  u8g2.print(" ");
}

//overload to provide default value for width
void drawLiveChannelMultiBar(int index, int value2){
  drawLiveChannelMultiBar(index, value2, 12);
}

void drawLiveChannelMultiBar(int index, int value2, int width){
  u8g2.print("Ch ");
  u8g2.print((int)index);
  u8g2.print(":");
  if(index<10){
    u8g2.print(" ");
  }
  u8g2PrintMultiBar(getOutputChannelValue(index), value2,outChannels[index].minRange, outChannels[index].maxRange, width);
  u8g2.print(" ");
}



//usb descriptor related helpers:
static void _convert_utf16le_to_utf8(const uint16_t *utf16, size_t utf16_len, uint8_t *utf8, size_t utf8_len) {
  // TODO: Check for runover.
  (void) utf8_len;
  // Get the UTF-16 length out of the data itself.

  for (size_t i = 0; i < utf16_len; i++) {
    uint16_t chr = utf16[i];
    if (chr < 0x80) {
      *utf8++ = chr & 0xff;
    } else if (chr < 0x800) {
      *utf8++ = (uint8_t) (0xC0 | (chr >> 6 & 0x1F));
      *utf8++ = (uint8_t) (0x80 | (chr >> 0 & 0x3F));
    } else {
      // TODO: Verify surrogate.
      *utf8++ = (uint8_t) (0xE0 | (chr >> 12 & 0x0F));
      *utf8++ = (uint8_t) (0x80 | (chr >> 6 & 0x3F));
      *utf8++ = (uint8_t) (0x80 | (chr >> 0 & 0x3F));
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int _count_utf8_bytes(const uint16_t *buf, size_t len) {
  size_t total_bytes = 0;
  for (size_t i = 0; i < len; i++) {
    uint16_t chr = buf[i];
    if (chr < 0x80) {
      total_bytes += 1;
    } else if (chr < 0x800) {
      total_bytes += 2;
    } else {
      total_bytes += 3;
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
  return total_bytes;
}

void utf16_to_utf8(uint16_t *temp_buf, size_t buf_len) {
  size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
  size_t utf8_len = _count_utf8_bytes(temp_buf + 1, utf16_len);

  _convert_utf16le_to_utf8(temp_buf + 1, utf16_len, (uint8_t *) temp_buf, buf_len);
  ((uint8_t *) temp_buf)[utf8_len] = '\0';
}

void print_device_descriptor(tuh_xfer_t *xfer) {
  if (XFER_RESULT_SUCCESS != xfer->result) {
    SerialDebugln<DEBUG_USB>("Failed to get device descriptor");
    return;
  }

  uint8_t const daddr = xfer->daddr;
  usb_dev_info_t *dev = &usb_raw_descriptors[daddr - 1];; //all instances will have the same descriptor
  tusb_desc_device_t *desc = &dev->desc_device;
  if(debug_level<DEBUG_USB>()){
    Serial.printf("Device %u: ID %04x:%04x\r\n", daddr, desc->idVendor, desc->idProduct);
    Serial.printf("Device Descriptor:\r\n");
    Serial.printf("  bLength             %u\r\n"     , desc->bLength);
    Serial.printf("  bDescriptorType     %u\r\n"     , desc->bDescriptorType);
    Serial.printf("  bcdUSB              %04x\r\n"   , desc->bcdUSB);
    Serial.printf("  bDeviceClass        %u\r\n"     , desc->bDeviceClass);
    Serial.printf("  bDeviceSubClass     %u\r\n"     , desc->bDeviceSubClass);
    Serial.printf("  bDeviceProtocol     %u\r\n"     , desc->bDeviceProtocol);
    Serial.printf("  bMaxPacketSize0     %u\r\n"     , desc->bMaxPacketSize0);
    Serial.printf("  idVendor            0x%04x\r\n" , desc->idVendor);
    Serial.printf("  idProduct           0x%04x\r\n" , desc->idProduct);
    Serial.printf("  bcdDevice           %04x\r\n"   , desc->bcdDevice);

    // Get String descriptor using Sync API
    Serial.printf("  iManufacturer       %u     ", desc->iManufacturer);
    if (XFER_RESULT_SUCCESS ==
        tuh_descriptor_get_manufacturer_string_sync(daddr, LANGUAGE_ID, dev->manufacturer, sizeof(dev->manufacturer))) {
      utf16_to_utf8(dev->manufacturer, sizeof(dev->manufacturer));
      Serial.printf((char *) dev->manufacturer);
    }
    Serial.printf("\r\n");

    Serial.printf("  iProduct            %u     ", desc->iProduct);
    if (XFER_RESULT_SUCCESS ==
        tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, dev->product, sizeof(dev->product))) {
      utf16_to_utf8(dev->product, sizeof(dev->product));
      Serial.printf((char *) dev->product);
    }
    Serial.printf("\r\n");

    Serial.printf("  iSerialNumber       %u     ", desc->iSerialNumber);
    if (XFER_RESULT_SUCCESS ==
        tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, dev->serial, sizeof(dev->serial))) {
      utf16_to_utf8(dev->serial, sizeof(dev->serial));
      Serial.printf((char *) dev->serial);
    }
    Serial.printf("\r\n");

    Serial.printf("  bNumConfigurations  %u\r\n", desc->bNumConfigurations);
  }
  // print device summary
  //print_lsusb();
}

//--------------------------------------------------------------------+
// TinyUSB Host callbacks
//--------------------------------------------------------------------+
extern "C"
{


//get USB device descriptor, currently mostly unused aside from debug logging
void tuh_mount_cb(uint8_t dev_addr) {
  Serial.printf("Device attached, address = %d\r\n", dev_addr);

  usb_dev_info_t *dev = &usb_raw_descriptors[dev_addr - 1];
  dev->mounted = true;

  // Get Device Descriptor
  tuh_descriptor_get_device(dev_addr, &dev->desc_device, 18, print_device_descriptor, 0);

}

  /*
  TODO: use tuh_descriptor_get_hid_report https://sourcevu.sysprogs.com/rp2040/lib/tinyusb/files/src/host/usbh.h#tok1113
  to get full HID report to dynamically populate controller data

  See also: https://github.com/hathach/tinyusb/blob/a0982cd5ca5727064520af418915e3d8efb204d3/src/device/usbd.h#L315

*/

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

  SerialDebugf<DEBUG_USB>("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);
  SerialDebugf<DEBUG_USB>("VID = %04x, PID = %04x\r\n", vid, pid);

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
      //store raw device descriptor and print it:
    //tuh_descriptor_get_device(dev_addr, &USBDeviceDescriptors[thisDeviceIndex].usb_dev_info, 18, print_device_descriptor, 0);
    //delay(100);
    snprintf(USBDeviceDescriptors[thisDeviceIndex].name, INPUT_NAME_LEN, "usb dev #(%d)", thisDeviceIndex);
  } else {
    SerialDebugln<DEBUG_USB>("Error: max number of device descriptors exceeded!");
    SerialDebugln<DEBUG_USB>("returning early from USB setup, may result in undefined behavior");
    return;
  }

  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    SerialDebug<DEBUG_USB>("HID Keyboard\r\n");
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      SerialDebug<DEBUG_USB>("Error: cannot request to receive report\r\n");
    }
    //init keyboard number input channel:
    AllocateUSBKeyboardNumberInputChannel(&inChannelsPool, 0, "USB KB number", &USBDeviceDescriptors[thisDeviceIndex]);

  } else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
    SerialDebug<DEBUG_USB>("HID Mouse\r\n");
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      SerialDebug<DEBUG_USB>("Error: cannot request to receive report\r\n");
    }
  } else if(const USBGamepadLayoutDefinition *thisControllerLayout = checkForKnownGamepadLayout(vid, pid); thisControllerLayout){
    USBDeviceDescriptors[thisDeviceIndex].hidInterfaceType = USB_DESCRIPTOR_PROTOCOL_GAMEPAD;
    USBDeviceDescriptors[thisDeviceIndex].layoutDef = thisControllerLayout;
    SerialDebug<DEBUG_USB>("found known gamepad: ");
    SerialDebugln<DEBUG_USB>(thisControllerLayout->name);
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      SerialDebug<DEBUG_USB>("Error: cannot request to receive report\r\n");
    }
    AllocateUSBGamepadStickChannels(&inChannelsPool, &USBDeviceDescriptors[thisDeviceIndex]);
  } else {
    SerialDebug<DEBUG_USB>("unknown HID device:");
    SerialDebugln<DEBUG_USB>(static_cast<uint32_t>(itf_protocol));
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  USBInputDeviceDescriptor* descriptor = findUSBDescriptorByDevAddrAndInstance(USBDeviceDescriptors, dev_addr, instance);
  if(descriptor != NULL){
    descriptor -> vid = 0; //mark this descriptor as unused
    releaseUSBInputChannels(&inChannelsPool, descriptor, outChannels, mixerChannels, failsafeChannels);
  } else {
    SerialDebug<DEBUG_USB>("Warning: unmounted device does not appear to have descriptor, check for erroneous logic");
  }
  //TODO: free context for inputs if allocated
  SerialDebugf<DEBUG_USB>("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

void handle_keyboard_key(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_t const *original_report) {
  USBInputDeviceDescriptor *descriptor = findUSBDescriptorByDevAddrAndInstance(USBDeviceDescriptors, dev_addr,instance);
  USBKeyboardContextType *context = (USBKeyboardContextType *)descriptor->inputChannels[0]->context;
  // only remap if not empty report i.e key released
  for (uint8_t i = 0; i < 6; i++) {
    if (original_report->keycode[i] != 0) {
      if(original_report->keycode[i] >= 30 && original_report->keycode[i] <=39){ //number keys on keyboard
        SerialDebug<DEBUG_USB_REPORT>("found key!");
        if(original_report->keycode[i] == 39){ //0 on keyboard
          context->latestValue = 0;
        } else {
          context->latestValue = (original_report->keycode[i] - 29); //shift by 29 to properly align numerical values
        }
      }
      SerialDebug<DEBUG_USB_REPORT>(static_cast<uint32_t>(original_report->keycode[i]));
      break;
    }
  }
}

//report len always expected to be 64 bytes
void handle_gamepad_input(USBInputDeviceDescriptor* thisDevice, uint8_t const *original_report, uint16_t len) {
  if(len > HID_REPORT_BUFSIZE){
    SerialDebug<DEBUG_USB_REPORT>("ERROR: invalid report length, truncating");
    len = HID_REPORT_BUFSIZE;
  }
  if (!thisDevice || !original_report){
    SerialDebug<DEBUG_USB_REPORT>("ERROR: device or original_report pointer is null");
    return;
  };
  memcpy(thisDevice->latest_report, original_report, len);
}


// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
  USBInputDeviceDescriptor* thisDevice = findUSBDescriptorByDevAddrAndInstance(USBDeviceDescriptors, dev_addr, instance);
  if (thisDevice -> hidInterfaceType == USB_DESCRIPTOR_PROTOCOL_KEYBOARD){
    if (len != 8) {
    SerialDebugf<DEBUG_USB_REPORT>("report len = %u NOT 8, not a keyboard!\r\n", len);
    printBytesAsHex((char *)report, len);
    } else {
      //hid_keyboard_report_t remapped_report;
      handle_keyboard_key(dev_addr, instance, (hid_keyboard_report_t const *) report);
    }
  } else if (thisDevice -> hidInterfaceType == USB_DESCRIPTOR_PROTOCOL_GAMEPAD){
    //Serial.print("report:");
    SerialDebug<DEBUG_USB_REPORT>("report:");
    if(debug_level<DEBUG_USB_REPORT>()){
      printBytesAsHex((char *)report, len);
    }
    if(len!=thisDevice->layoutDef->reportLength){
      //TODO: optionally recognize and handle double-packets, such as captured "report:03 0F 00 A9 7F 7F 00 00 00 00 28 03 0F 00 A9 7F 7F 00 00 00 00 28 -- report len = 22 NOT 11, not matching configured gamepad!"
      SerialDebugf<DEBUG_USB_REPORT>("report len = %u NOT %u, not matching configured gamepad!\r\n", len, thisDevice->layoutDef->reportLength);
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
