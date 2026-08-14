#pragma once

#include <Arduino.h>

// Undefine conflicting macros from Arduino core AFTER Arduino.h
#undef max
#undef min
#undef isspace
#undef isprint
#undef isupper
#undef islower
#undef isalpha
#undef isdigit
#undef isxdigit
#undef isalnum
#undef iscntrl
#undef isgraph
#undef ispunct
#undef tolower
#undef toupper

#include <WiFi.h>
#include <WiFiSSLClient.h>
#include <PubSubClient.h>
#include <vector>
#include <ArduinoJson.h>
#include "bw16_commands.h"
#include "wifi_conf.h"
#include "wifi_structures.h"

// Attack target structure
struct AttackTarget {
  uint8_t bssid[6];
  uint16_t channel;
  String ssid;
  
  AttackTarget() : channel(0), ssid("") {
    memset(bssid, 0, sizeof(bssid));
  }
};

// Scanned network structure
struct ScannedNetwork {
  String ssid;
  String bssid;
  int rssi;
  uint8_t channel;
  bool isTarget;
};

class RemoteControl {
public:
  static void begin();
  static void loop();
  static bool shouldAttack() { return attackTriggered; }
  static bool isAttackActive() { return attackModeActive; }
  static void runAttackLoop();
  
  // Public for MQTT callback access
  static bool attackTriggered;
  static bool attackModeActive;
  static bool attackPaused;
  static std::vector<AttackTarget> attackTargets;
  static std::vector<AttackTarget> savedAttackTargets;
  static uint16_t savedReason;
  static uint16_t lastReason;
  static unsigned long checkInterval;
  static unsigned long lastCheckTime;
  static unsigned long lastStatusTime;
  static WiFiSSLClient wifiClient;
  static PubSubClient mqtt;
  
private:
  static std::vector<ScannedNetwork> scanResults;
  static volatile bool scanComplete;
  static rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scanResult);
  static bool connectMQTT();
  static void performMQTTCheckIn();
  static void handleStandbyMode(unsigned long currentTime);
  static void handlePausedMode();
  static void sendStatus();
  static void handleCommand(char* topic, byte* payload, unsigned int length);
  static String sanitizeSsid(const String& ssid);
  static String localIpString();
  static void publishStatus(const char* status);
  static void publishScanResults();
  static bool scanNetworks();
  static void executeAttack(const std::vector<AttackTarget>& targets, uint16_t reason);
  static void stopAttack();
};