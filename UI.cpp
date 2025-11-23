#include <stdint.h>
#include <cstddef>
#include <Arduino.h>

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

void printStructWithLenAsHex(void* ptr, size_t length){
  char* charArray = (char*)ptr;  // Typecast to char pointer
  printBytesAsHex(charArray, length);
}