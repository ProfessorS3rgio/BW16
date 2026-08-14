#ifndef WIFI_MANAGER_ATTACK_H
#define WIFI_MANAGER_ATTACK_H

#include <Arduino.h>
#include "wifi_conf.h"
#include "wifi_structures.h"

// WiFi Manager Attack class for better band handling
class WiFiManagerAttack {
private:
  bool radioInitialized = false;
  int currentChannel = 0;
  
public:
  // Initialize the radio for attack mode
  bool begin() {
    Serial.println("Initializing WiFi for attack mode...");
    
    // Disconnect from current AP
    wifi_disconnect();
    delay(200);
    
    // Turn off radio briefly to reset
    wifi_off();
    delay(100);
    
    // Turn radio back on in station mode
    wifi_on(RTW_MODE_STA);
    delay(500);  // Give radio time to initialize
    
    radioInitialized = true;
    Serial.println("WiFi radio initialized for attack mode");
    return true;
  }
  
  // Set the channel for attack (handles both 2.4GHz and 5GHz)
  bool setChannel(uint16_t channel) {
    if (!radioInitialized) {
      Serial.println("Radio not initialized");
      return false;
    }
    
    Serial.print("Setting channel to: ");
    Serial.println(channel);
    
    // Try multiple methods to set channel
    int result = -1;
    
    // Method 1: Use wifi_set_channel
    result = wifi_set_channel(channel);
    if (result == 0) {
      Serial.println("Channel set via wifi_set_channel");
      currentChannel = channel;
      delay(100);  // Let channel change take effect
      return true;
    }
    
    // Method 2: Try turning off, set channel, turn on
    wifi_off();
    delay(50);
    result = wifi_set_channel(channel);
    wifi_on(RTW_MODE_STA);
    delay(300);
    
    if (result == 0) {
      Serial.println("Channel set via radio reset");
      currentChannel = channel;
      return true;
    }
    
    // Method 3: Try using wext_set_channel (declared in wifi_util.h)
    result = wext_set_channel("wlan0", channel);
    if (result == 0) {
      Serial.println("Channel set via wext_set_channel");
      currentChannel = channel;
      delay(100);
      return true;
    }
    
    Serial.println("Failed to set channel");
    return false;
  }
  
  // Get current channel
  int getChannel() {
    return currentChannel;
  }
  
  // Clean up and restore normal WiFi operation
  void end() {
    Serial.println("Restoring normal WiFi operation...");
    
    // Turn off radio
    wifi_off();
    delay(100);
    
    // Turn back on in station mode
    wifi_on(RTW_MODE_STA);
    delay(500);
    
    radioInitialized = false;
    currentChannel = 0;
  }
  
  // Check if radio is initialized
  bool isInitialized() {
    return radioInitialized;
  }
};

#endif