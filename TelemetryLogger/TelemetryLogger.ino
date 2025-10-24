/*
  DigitalReadSerial

  Reads a digital input on pin 2, prints the result to the Serial Monitor

  This example code is in the public domain.

  https://docs.arduino.cc/built-in-examples/basics/DigitalReadSerial/
*/

int serialModuleRX = 16;

// the setup routine runs once when you press reset:
void setup() {
  //initialize Serial1 on GPIO 16 and 17 (pins 21 and 22), at 100kbaud, even parity
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
  if (Serial1.available()){
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
      }
    }
  }
  //Serial.println("looping");
  delay(1);  // delay in between reads for stability
}
