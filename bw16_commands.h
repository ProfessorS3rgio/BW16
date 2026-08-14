#pragma once

#include <Arduino.h>
#include <vector>

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
  
  // Multiple targets support
  struct TargetInfo {
    uint8_t mac[6];
    uint16_t channel;
    String ssid;
    
    TargetInfo() : channel(0), ssid("") {
      memset(mac, 0, sizeof(mac));
    }
  };
  std::vector<TargetInfo> targets;
  
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

inline bool parseTargetInfo(const String &targetStr, Bw16Command::TargetInfo &target) {
  // Format: MAC,channel,SSID
  // Example: 10:08:1D:0A:52:38,6,Printer
  
  int firstComma = targetStr.indexOf(',');
  int secondComma = targetStr.indexOf(',', firstComma + 1);
  
  if (firstComma <= 0 || secondComma <= firstComma + 1) {
    Serial.println("Invalid target format");
    return false;
  }
  
  String macStr = targetStr.substring(0, firstComma);
  String channelStr = targetStr.substring(firstComma + 1, secondComma);
  String ssidStr = targetStr.substring(secondComma + 1);
  
  if (!parseMacAddress(macStr, target.mac)) {
    Serial.println("Failed to parse target MAC");
    return false;
  }
  
  target.channel = (uint16_t)channelStr.toInt();
  target.ssid = ssidStr;
  target.ssid.trim();
  
  // Validate channel
  if (target.channel < 1 || target.channel > 165) {
    Serial.print("Invalid channel: ");
    Serial.println(target.channel);
    return false;
  }
  
  Serial.print("Parsed target: SSID=");
  Serial.print(target.ssid);
  Serial.print(", Channel=");
  Serial.println(target.channel);
  
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
  // Format: multi_attack:count,MAC,channel,SSID|MAC,channel,SSID|MAC,channel,SSID
  // Example: multi_attack:5,10:08:1D:0A:52:3C,132,Printer_5G|10:08:1D:0A:52:38,6,Printer
  if (command.startsWith("multi_attack:")) {
    String params = command.substring(13); // Remove "multi_attack:"
    
    // Find the first comma to get the count
    int firstComma = params.indexOf(',');
    
    if (firstComma <= 0) {
      Serial.println("Invalid multi_attack format - no count");
      cmd.type = Bw16CommandType::Unsupported;
      return cmd;
    }
    
    // Parse count
    String countStr = params.substring(0, firstComma);
    int count = countStr.toInt();
    
    // Validate count
    if (count <= 0 || count > 100) {
      Serial.print("Invalid attack count: ");
      Serial.println(count);
      cmd.type = Bw16CommandType::Unsupported;
      return cmd;
    }
    
    cmd.attackCount = (uint8_t)count;
    
    // Get the targets part (after the count and comma)
    String targetsPart = params.substring(firstComma + 1);
    
    Serial.print("Targets part: '");
    Serial.print(targetsPart);
    Serial.println("'");
    
    // Split targets by pipe character
    int pipePos = 0;
    unsigned int startPos = 0;
    bool firstTarget = true;
    
    while (startPos < targetsPart.length()) {
      pipePos = targetsPart.indexOf('|', startPos);
      
      String targetStr;
      if (pipePos > 0) {
        targetStr = targetsPart.substring(startPos, pipePos);
        startPos = pipePos + 1;
      } else {
        targetStr = targetsPart.substring(startPos);
        startPos = targetsPart.length();
      }
      
      targetStr.trim();
      if (targetStr.length() == 0) continue;
      
      Serial.print("Parsing target: '");
      Serial.print(targetStr);
      Serial.println("'");
      
      Bw16Command::TargetInfo target;
      if (parseTargetInfo(targetStr, target)) {
        cmd.targets.push_back(target);
        
        // Set the first target as the primary target for backward compatibility
        if (firstTarget) {
          memcpy(cmd.targetMac, target.mac, 6);
          cmd.channel = target.channel;
          cmd.targetSsid = target.ssid;
          firstTarget = false;
        }
      } else {
        Serial.println("Failed to parse target");
      }
    }
    
    // Check if we parsed at least one target
    if (cmd.targets.size() > 0) {
      cmd.type = Bw16CommandType::MultiAttack;
      Serial.print("Parsed ");
      Serial.print(cmd.targets.size());
      Serial.println(" attack targets");
      return cmd;
    }
    
    // If we get here, the multi_attack command was malformed
    Serial.println("No valid targets found in multi_attack command");
    cmd.type = Bw16CommandType::Unsupported;
    return cmd;
  }

  cmd.type = Bw16CommandType::Unsupported;
  return cmd;
}