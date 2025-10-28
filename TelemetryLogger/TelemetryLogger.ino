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


//data structures for status and output
char capturedData[MAX_TELEM_DATA_BYTES + 1]; // Pre-allocated buffer, +1 for null-terminator
MultiModuleStatus moduleStatus;

MultiProtocolStream streamOut;



// the setup routine runs once when you press reset:
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
  streamOut.header.is_failsafe=0;
  setProtocolMode(streamOut, 28, 0); //a single protocol hardcoded for now, 28=AFHDS2A, 0=PWM_IBUS
  //TODO init remaining configuration flags for streamOut

  while (!Serial) {
    ; // wait for USB serial port to connect
  }
  Serial.println("start telemetry log.");
  

}

/*note that protocol and subProtocol names are inconsistient in documentation, and thus some of the struct names may be confusing.
In the multi-module docs,
protocolNum is sometimes referred to as "subProtocol" but other times as "protocol"
and subprotocolNum is sometimes referred to as "type" but other times as "subProtocol"
*/
void setProtocolMode(MultiProtocolStream s, uint8_t protocolNum, uint8_t subprotocolNum){
  s.sub_protocol_flags.sub_protocol = protocolNum & 0x1F; //bits 0-4
  s.header.sub_protocol_range = (protocolNum >> 5) & 0x01; //bit 5
  s.extended_protocol.sub_protocol = (protocolNum >> 6) & 0x03; //bits 6-7
  s.rx_num_power_type.type = subprotocolNum & 0x07; //type is 3 bits total
}

// the loop routine runs over and over again forever:
void loop() {
  // Update each button's state
  okButton.update();
  backButton.update();
  upButton.update();
  downButton.update();
  leftButton.update();
  rightButton.update();

    if (okButton.pressed()) {
        Serial.println("OK Button Pressed!");
    }

    if (backButton.pressed()) {
        Serial.println("Back Button Pressed!");
    }

    if (upButton.pressed()) {
        Serial.println("Up Button Pressed!");
    }

    if (downButton.pressed()) {
        Serial.println("Down Button Pressed!");
    }

    if (leftButton.pressed()) {
        Serial.println("Left Button Pressed!");
    }

    if (rightButton.pressed()) {
        Serial.println("Right Button Pressed!");
    }


  getTelemetry();
  if(Serial.available()){ //commands from computer

  }
  //Serial.println("looping");
  //delay(1);  // delay in between reads for stability
  u8g2.sendBuffer();
}

int getComputerCommand(){
  return 0;
}

int getTelemetry(){
  //if starting program and no telem received
  u8g2.setCursor(0,5);
  if(millis() < 15000 && lastTelemetryMillis == 0){
    u8g2.print("waiting for telemetry...");
  } else if (millis() - lastTelemetryMillis > 15000) {
    u8g2.print("no telemetry available...");
  }
  if (SerialModule.available() >= 4){
    //Serial.println("4 bytes");
    if (SerialModule.read() == 'M'){
      if(SerialModule.read() == 'P'){

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
        lastTelemetryMillis = millis();
        delay(5); //give data time to populate
        if (SerialModule.available() < dataLength) {
          Serial.println("Not enough data available!"); //note: if this occurs often, add a 10 microsecond delay
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
  Serial.print(status.next_protocol);
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

