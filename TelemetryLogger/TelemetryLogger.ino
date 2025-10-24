/*
  DigitalReadSerial

  Reads a digital input on pin 2, prints the result to the Serial Monitor

  This example code is in the public domain.

  https://docs.arduino.cc/built-in-examples/basics/DigitalReadSerial/
*/

#include "MultiModuleTypes.h"

char capturedData[MAX_TELEM_DATA_BYTES + 1]; // Pre-allocated buffer, +1 for null-terminator

// the setup routine runs once when you press reset:
void setup() {
  //initialize Serial1 on GPIO 16 and 17 (pins 21 and 22), at 100kbaud, even parity, 2 stop bits
  Serial1.setRX(17);
  Serial1.setTX(16);
  Serial1.setInvertRX(true);
  Serial1.begin(100000, SERIAL_8E2);
  Serial.begin(); //init USB serial
  while (!Serial) {
    ; // wait for USB serial port to connect
  }
  Serial.println("start telemetry log.");

}

// the loop routine runs over and over again forever:
void loop() {
  getTelemetry();
  //Serial.println("looping");
  delay(1);  // delay in between reads for stability
}

int getTelemetry(){
  if (Serial1.available() >= 4){
    //Serial.println("4 bytes");
    if (Serial1.read() == 'M'){
      if(Serial1.read() == 'P'){
        Serial.println("found valid header!");
        char type = Serial1.read();
        char length = Serial1.read();
        // Convert length byte to an integer for data capture
        int dataLength = (int)length;
        Serial.print("type:");
        Serial.println((int)type);
        Serial.print("length:");
        Serial.println(dataLength);
        if (dataLength <= 0 || dataLength > MAX_TELEM_DATA_BYTES) {
          Serial.println("Invalid data length!");
          return -1; // return error status
        }
        delay(4); //give data time to populate
        if (Serial1.available() < dataLength) {
          Serial.println("Not enough data available!"); //note: if this occurs often, add a 10 microsecond delay
          Serial.print("expected ");
          Serial.print(dataLength);
          Serial.print(" bytes but only ");
          Serial.print(Serial1.available());
          Serial.println(" available.");
          return -2;
        }
        
        Serial1.readBytes(capturedData, dataLength);
        capturedData[dataLength] = '\0'; // Null-terminate the string
        printBytesAsHex(capturedData, dataLength);
        
      }
    }
  }
  return 0;
}

void printBytesAsHex(const char* data, int length) {
    Serial.print("Captured Data in Hex: ");
    for (int i = 0; i < length; i++) {
        // Print each byte as a two-digit hexadecimal value
        Serial.print(data[i], HEX); // Print in hex
        Serial.print(" "); // Space for readability
    }
    Serial.println(); // New line at the end
}
