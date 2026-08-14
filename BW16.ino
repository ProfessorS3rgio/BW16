#include "bw16_remote_control.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  
  RemoteControl::begin();
}

void loop() {
  RemoteControl::loop();
  delay(10);
}