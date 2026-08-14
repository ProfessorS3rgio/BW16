#pragma once

#define WIFI_SSID "YourWiFiName"
#define WIFI_PASS "YourWiFiPassword"

#define MQTT_USER "YourMQTTUsername"
#define MQTT_PASS "YourMQTTPassword"

// Optional PEM root certificate. When omitted, TLS traffic is encrypted but
// the broker certificate is not authenticated, matching the ESP32 setup.
// #define MQTT_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
