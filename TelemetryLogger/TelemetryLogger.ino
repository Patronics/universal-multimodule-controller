/*
TelemetryLogger.ino
Written By Patrick Leiser
Runs on Raspberry Pi Pico or similar RP2040 family boards
This script reads telemetry data from a 4-in-one multimodule.
*/

#include "MultiModuleTypes.h"

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
#define SerialModule Serial2
//comment out the following defines if on a non-RP2040 platform where UART pins are fixed
#define SERIAL_MODULE_RX_PIN 5
#define SERIAL_MODULE_TX_PIN 4
#define SERIAL_MODULE_INVERT_RX

const unsigned long SERIAL_MODULE_BAUD_RATE = 100000;
const int SERIAL_MODULE_BUFFER_SIZE = 128;

const int DISPLAY_STR_BUFFER_SIZE = 32;
const int CHAR_WIDTH = 4;


unsigned long lastTelemetryMillis = 0;
unsigned long lastLoopMillis = 0;
unsigned long lastTxMillis = 0;

const unsigned long msBetweenTxUpdates = 7; 

int currentMenu = 0;

//setup i2c display
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

//UI buttons
#define OK_BUTTON_PIN 11
#define BACK_BUTTON_PIN 15
#define UP_BUTTON_PIN 10
#define DOWN_BUTTON_PIN 12
#define LEFT_BUTTON_PIN 14
#define RIGHT_BUTTON_PIN 13

const uint16_t BUTTON_DEBOUNCE_INTERVAL = 5;

Bounce2::Button okButton;
Bounce2::Button backButton;
Bounce2::Button upButton;
Bounce2::Button downButton;
Bounce2::Button leftButton;
Bounce2::Button rightButton;

enum NavButton {
  NO_BUTTON_PRESSED,  //value corresponds to 0, so falsy
  OK_BUTTON,          //value 1, etc.
  BACK_BUTTON,
  UP_BUTTON,
  DOWN_BUTTON,
  LEFT_BUTTON,
  RIGHT_BUTTON
};


//data structures for status and output
char capturedData[MAX_TELEM_DATA_BYTES + 1]; // Pre-allocated buffer, +1 for null-terminator
MultiModuleStatus moduleStatus;

MultiProtocolStream streamOut;
uint8_t streamOutAdditionalProtocolDataLen = 0;

bool transmitActive = false;
bool haveTelemetry = false;



void setup() {

  //bounce2 setup
  okButton.attach(OK_BUTTON_PIN, INPUT_PULLDOWN);
  backButton.attach(BACK_BUTTON_PIN, INPUT_PULLDOWN);
  upButton.attach(UP_BUTTON_PIN, INPUT_PULLDOWN);
  downButton.attach(DOWN_BUTTON_PIN, INPUT_PULLDOWN);
  leftButton.attach(LEFT_BUTTON_PIN, INPUT_PULLDOWN);
  rightButton.attach(RIGHT_BUTTON_PIN, INPUT_PULLDOWN);

  //debounce interval
  okButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  backButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  upButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  downButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  leftButton.interval(BUTTON_DEBOUNCE_INTERVAL);
  rightButton.interval(BUTTON_DEBOUNCE_INTERVAL);

  okButton.setPressedState(HIGH);
  backButton.setPressedState(HIGH);
  upButton.setPressedState(HIGH);
  downButton.setPressedState(HIGH);
  leftButton.setPressedState(HIGH);
  rightButton.setPressedState(HIGH);


  //set i2c pins for display
  Wire.setSDA(20);
  Wire.setSCL(21);
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
  Serial.begin(); //init USB serial
  //initialize stream values
  //start setting up output data structures
  streamOut.header.reserved_bits=0b010101;
  streamOut.header.is_failsafe = 0;
  setProtocolMode(&streamOut, 28, 0); //a single protocol hardcoded for now, 28=AFHDS2A, 0=PWM_IBUS
  streamOut.rx_num_power_type.rxNum = 0;
  streamOut.rx_num_power_type.power = 0; //high power
  streamOut.option_protocol = -128; //unknown purpose, matching example from transmitter for now
  streamOut.extended_protocol.telemetry_Invert = 1;
  //TODO init remaining configuration flags for streamOut

  /*while (!Serial) {
    ; // wait for USB serial port to connect
  }*/
  Serial.println("start telemetry log.");
  Serial.print("output struct:");
  printStructWithLenAsHex(&streamOut, (sizeof(streamOut)-(9-streamOutAdditionalProtocolDataLen)));
  redrawMenu(0);
}


void loop() {
  // Update each button's state
  okButton.update();
  backButton.update();
  upButton.update();
  downButton.update();
  leftButton.update();
  rightButton.update();

  NavButton currentNavButton = NO_BUTTON_PRESSED;
  if (okButton.pressed()) {
    currentNavButton = OK_BUTTON;
  }
  if (backButton.pressed()) {
    currentNavButton = BACK_BUTTON;
  }
  if (upButton.pressed()) {
    currentNavButton = UP_BUTTON;
    Serial.println("Up Button Pressed!");
  }
  if (downButton.pressed()) {
    currentNavButton = DOWN_BUTTON;
    Serial.println("Down Button Pressed!");
  }
  if (leftButton.pressed()) {
    currentNavButton = LEFT_BUTTON;
    Serial.println("Left Button Pressed!");
  }
  if (rightButton.pressed()) {
    currentNavButton = RIGHT_BUTTON;
    Serial.println("Right Button Pressed!");
  }
  if(currentNavButton){
    handleNavButton(currentNavButton);
  }

  getTelemetry();
  /*if(Serial.available()){ //commands from computer

  }*/

  if(transmitActive && millis() - lastTxMillis >= msBetweenTxUpdates){
    lastTxMillis = millis();
    transmit(&streamOut, streamOutAdditionalProtocolDataLen);

  }

  //Serial.print("loop time: ");
  //Serial.println(millis()-lastLoopMillis);
  lastLoopMillis = millis();
}

void transmit(MultiProtocolStream* s, uint8_t aditional_bytes){
  uint8_t* byteArray = (uint8_t*)s;
  SerialModule.write(byteArray, (sizeof(MultiProtocolStream)-(9-aditional_bytes)));
}

void drawMenuItem(int x, int y, const char* label, bool selected) {
  u8g2.setCursor(x, y);
  if (selected) {
    u8g2.print("> "); // Add a marker for the selected item
  } else {
    u8g2.print("  "); // No marker for unselected items
  }
  u8g2.print(label); // Print the label of the menu item
}

void redrawMenu(int selectedMenuItem){
  drawMenuItem(0,  25, "Protocol", selectedMenuItem==0);
  drawMenuItem(64, 25, "Sub-protocol", selectedMenuItem==1);
  drawMenuItem(0,  30, "Recv number", selectedMenuItem==2);
  drawMenuItem(64, 30, "Optn-protocol", selectedMenuItem==3);
  drawMenuItem(0,  35, "Channel Map", selectedMenuItem==4);
  drawMenuItem(64, 35, "Channel Range", selectedMenuItem==5);
  drawMenuItem(0,  40, "Values", selectedMenuItem==6);
  drawMenuItem(64, 40, "Failsafe Value", selectedMenuItem==7);
}

void handleNavButton(NavButton btn){
  Serial.println(btn);
  if(btn == UP_BUTTON){
    currentMenu -= 2;
    if(currentMenu < 0){
      currentMenu = 0;
    }
  } else if(btn == DOWN_BUTTON){
    currentMenu += 2;
  } else if(btn == LEFT_BUTTON){
    currentMenu -= 1;
  } else if(btn == RIGHT_BUTTON){
    currentMenu += 1;
  } else if(btn == OK_BUTTON){
    transmitActive = true;
    u8g2.drawStr(55, 55, "tx   active");
    Serial.println("OK Button Pressed!");
  } else if (btn == BACK_BUTTON){
      Serial.println("Back Button Pressed!");
      u8g2.drawStr(55, 55, "tx inactive");
      transmitActive = false;
  }

  redrawMenu(currentMenu);
  u8g2.sendBuffer();
}


/*note that protocol and subProtocol names are inconsistient in documentation, and thus some of the struct names may be confusing.
In the multi-module docs,
protocolNum is sometimes referred to as "subProtocol" but other times as "protocol"
and subprotocolNum is sometimes referred to as "type" but other times as "subProtocol"
*/
void setProtocolMode(MultiProtocolStream *s, uint8_t protocolNum, uint8_t subprotocolNum){
  s->sub_protocol_flags.sub_protocol = protocolNum & 0x1F; //bits 0-4
  s->header.sub_protocol_range_inv = ((protocolNum >> 5) ^ 0x01) & 0x01; //inverted bit 5
  s->extended_protocol.sub_protocol = (protocolNum >> 6) & 0x03; //bits 6-7
  s->rx_num_power_type.type = subprotocolNum & 0x07; //type is 3 bits total
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

void printStructWithLenAsHex(void* ptr, size_t length){
  char* charArray = (char*)ptr;  // Typecast to char pointer
  printBytesAsHex(charArray, length);
}

void printBytesAsHex(const char* data, int length) {
    for (int i = 0; i < length; i++) {
        // Print each byte as a two-digit hexadecimal value
        if(data[i]<0x10){
          Serial.print('0');
        }
        Serial.print(data[i], HEX); // Print in hex
        Serial.print(" "); // Space for readability
    }
    Serial.println(); // New line at the end
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

