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

inline uint8_t hexCharToByte(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

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
  
  // Manual parsing without sscanf
  for (int i = 0; i < 6; i++) {
    int pos = i * 3;
    
    // Check for colon separator (except before first byte)
    if (i > 0 && cleanMac.charAt(pos - 1) != ':') {
      Serial.print("Missing colon at position ");
      Serial.println(pos - 1);
      return false;
    }
    
    // Parse two hex characters
    char highChar = cleanMac.charAt(pos);
    char lowChar = cleanMac.charAt(pos + 1);
    
    // Validate hex characters
    if (!((highChar >= '0' && highChar <= '9') || 
          (highChar >= 'a' && highChar <= 'f') || 
          (highChar >= 'A' && highChar <= 'F'))) {
      Serial.print("Invalid hex character: '");
      Serial.print(highChar);
      Serial.println("'");
      return false;
    }
    
    if (!((lowChar >= '0' && lowChar <= '9') || 
          (lowChar >= 'a' && lowChar <= 'f') || 
          (lowChar >= 'A' && lowChar <= 'F'))) {
      Serial.print("Invalid hex character: '");
      Serial.print(lowChar);
      Serial.println("'");
      return false;
    }
    
    mac[i] = (hexCharToByte(highChar) << 4) | hexCharToByte(lowChar);
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
    
    // Find the commas
    int firstComma = params.indexOf(',');
    int secondComma = params.indexOf(',', firstComma + 1);
    
    if (firstComma > 0 && secondComma > firstComma + 1) {
      // Parse count
      String countStr = params.substring(0, firstComma);
      int count = countStr.toInt();
      
      // Parse MAC address
      String macStr = params.substring(firstComma + 1, secondComma);
      
      // Parse channel and SSID
      String channelAndSsid = params.substring(secondComma + 1);
      int thirdComma = channelAndSsid.indexOf(',');
      
      if (thirdComma > 0) {
        String channelStr = channelAndSsid.substring(0, thirdComma);
        String ssidStr = channelAndSsid.substring(thirdComma + 1);
        
        // Validate all parameters
        if (count > 0 && count <= 100 && // Limit attack count
            parseMacAddress(macStr, cmd.targetMac) &&
            channelStr.length() > 0) {
          
          cmd.type = Bw16CommandType::MultiAttack;
          cmd.attackCount = (uint8_t)count;
          cmd.channel = (uint16_t)channelStr.toInt();
          cmd.targetSsid = ssidStr;
          cmd.targetSsid.trim();
          
          return cmd;
        }
      }
    }
    
    // If we get here, the multi_attack command was malformed
    cmd.type = Bw16CommandType::Unsupported;
    return cmd;
  }

  cmd.type = Bw16CommandType::Unsupported;
  return cmd;
}