#pragma once

#include <Arduino.h>

enum class Bw16CommandType {
  Empty,
  Scan,
  Status,
  Restart,
  Stop,
  SetInterval,
  MultiAttack,
  Unsupported
};

struct Bw16Command {
  Bw16CommandType type;
  unsigned long intervalMs;
  
  // Multi-attack parameters
  uint8_t attackCount;
  uint8_t targetMac[6];
  uint16_t channel;
  String targetSsid;
  
  // Constructor with default values
  Bw16Command() : type(Bw16CommandType::Empty), intervalMs(0), 
                  attackCount(0), channel(0), targetSsid("") {
    memset(targetMac, 0, sizeof(targetMac));
  }
};

inline bool parseMacAddress(const String &macStr, uint8_t *mac) {
  // Remove any whitespace
  String cleanMac = macStr;
  cleanMac.trim();
  
  // Expects format: XX:XX:XX:XX:XX:XX (17 characters)
  if (cleanMac.length() != 17) {
    Serial.print("MAC length error: ");
    Serial.print(cleanMac.length());
    Serial.print(" - '");
    Serial.print(cleanMac);
    Serial.println("'");
    return false;
  }
  
  // Check for colons at positions 2, 5, 8, 11, 14
  for (int i = 2; i <= 14; i += 3) {
    if (cleanMac.charAt(i) != ':') {
      Serial.print("MAC format error at position ");
      Serial.println(i);
      return false;
    }
  }
  
  // Parse each hex pair
  int values[6];
  int result = sscanf(cleanMac.c_str(), "%x:%x:%x:%x:%x:%x",
                      &values[0], &values[1], &values[2],
                      &values[3], &values[4], &values[5]);
  
  if (result != 6) {
    Serial.print("sscanf parsed only ");
    Serial.print(result);
    Serial.println(" values");
    return false;
  }
  
  // Validate and assign
  for (int i = 0; i < 6; i++) {
    if (values[i] < 0 || values[i] > 255) {
      Serial.print("MAC value out of range: ");
      Serial.println(values[i]);
      return false;
    }
    mac[i] = (uint8_t)values[i];
  }
  
  // Debug output
  Serial.print("Parsed MAC: ");
  for (int i = 0; i < 6; i++) {
    if (i > 0) Serial.print(":");
    if (mac[i] < 0x10) Serial.print("0");
    Serial.print(mac[i], HEX);
  }
  Serial.println();
  
  return true;
}

inline Bw16Command parseBw16Command(String command) {
  Bw16Command cmd;
  
  command.trim();
  if (command.length() == 0) {
    cmd.type = Bw16CommandType::Empty;
    return cmd;
  }
  
  if (command == "scan") {
    cmd.type = Bw16CommandType::Scan;
    return cmd;
  }
  
  if (command == "status") {
    cmd.type = Bw16CommandType::Status;
    return cmd;
  }
  
  if (command == "restart") {
    cmd.type = Bw16CommandType::Restart;
    return cmd;
  }
  
  if (command == "stop_attack") {
    cmd.type = Bw16CommandType::Stop;
    return cmd;
  }

  if (command.startsWith("set_interval:")) {
    cmd.type = Bw16CommandType::SetInterval;
    cmd.intervalMs = command.substring(13).toInt();
    return cmd;
  }
  
  // Parse multi_attack command
  // Format: multi_attack:count,MAC,channel,SSID
  // Example: multi_attack:5,10:08:1D:0A:52:3C,132,Printer_5G
  if (command.startsWith("multi_attack:")) {
    String params = command.substring(13); // Remove "multi_attack:"
    
    Serial.print("Multi-attack params: '");
    Serial.print(params);
    Serial.println("'");
    
    // Find the commas
    int firstComma = params.indexOf(',');
    int secondComma = params.indexOf(',', firstComma + 1);
    
    Serial.print("First comma at: ");
    Serial.println(firstComma);
    Serial.print("Second comma at: ");
    Serial.println(secondComma);
    
    if (firstComma > 0 && secondComma > firstComma + 1) {
      // Parse count
      String countStr = params.substring(0, firstComma);
      int count = countStr.toInt();
      Serial.print("Count: ");
      Serial.println(count);
      
      // Parse MAC address
      String macStr = params.substring(firstComma + 1, secondComma);
      Serial.print("MAC string: '");
      Serial.print(macStr);
      Serial.println("'");
      
      // Parse channel and SSID
      String channelAndSsid = params.substring(secondComma + 1);
      int thirdComma = channelAndSsid.indexOf(',');
      
      if (thirdComma > 0) {
        String channelStr = channelAndSsid.substring(0, thirdComma);
        String ssidStr = channelAndSsid.substring(thirdComma + 1);
        
        Serial.print("Channel string: '");
        Serial.print(channelStr);
        Serial.println("'");
        Serial.print("SSID string: '");
        Serial.print(ssidStr);
        Serial.println("'");
        
        // Validate all parameters
        if (count > 0 && count <= 100 && // Limit attack count
            parseMacAddress(macStr, cmd.targetMac) &&
            channelStr.length() > 0) {
          
          cmd.type = Bw16CommandType::MultiAttack;
          cmd.attackCount = (uint8_t)count;
          cmd.channel = (uint16_t)channelStr.toInt();
          cmd.targetSsid = ssidStr;
          cmd.targetSsid.trim();
          
          Serial.println("Multi-attack command parsed successfully");
          return cmd;
        } else {
          Serial.println("Multi-attack validation failed");
        }
      } else {
        Serial.println("Could not find third comma");
      }
    } else {
      Serial.println("Could not find proper commas");
    }
    
    // If we get here, the multi_attack command was malformed
    cmd.type = Bw16CommandType::Unsupported;
    return cmd;
  }

  cmd.type = Bw16CommandType::Unsupported;
  return cmd;
}