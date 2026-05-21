#include <Wire.h>

void setup() {
  Wire.begin(0x08);  // join I2C bus as slave at address 0x08
  Wire.onRequest(requestEvent);
}

void loop() {}

void requestEvent() {
  Wire.write(0x42);  // send one byte when requested
} 