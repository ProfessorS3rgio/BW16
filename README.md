# Vendo Ghost BW16 MQTT Scanner

BW16 (RTL8720DN) firmware for remotely inventorying authorized 2.4 GHz and
5 GHz WiFi environments over MQTT. It follows the ESP32 project's command,
status, and scan topic conventions while using the AmebaD WiFi APIs.

This firmware runs only in WiFi station mode. It does not create a hotspot or
start a web server, and the active sketch does not include packet-injection
code.

## Requirements

- Ai-Thinker BW16 (RTL8720DN)
- Arduino IDE or Arduino CLI
- AmebaD board package 3.1.9 or newer
- ArduinoJson
- PubSubClient (the AmebaD package also provides a compatible version)
- An MQTT broker account

Add the AmebaD board manager URL in Arduino IDE:

```text
https://raw.githubusercontent.com/Ameba-AIoT/ameba-arduino-d/master/Arduino_package/package_realtek_amebad_index.json
```

## Configure

1. Copy `bw16_secrets.example.h` to `bw16_secrets.h` if the local file does
   not already exist.
2. Set `WIFI_SSID`, `WIFI_PASS`, `MQTT_USER`, and `MQTT_PASS`.
3. Set a unique `DEVICE_ID` in `bw16_config.h`.
4. Adjust `MQTT_BROKER` and `MQTT_PORT` if needed.

The default TLS behavior matches the ESP32 firmware's insecure TLS mode:
traffic is encrypted, but the broker certificate is not authenticated. Define
`MQTT_ROOT_CA` in `bw16_secrets.h` to enable certificate validation.

## MQTT Topics

For the default device ID `bw16-01`:

```text
vendoghost/bw16-01/command
vendoghost/bw16-01/command/retained
vendoghost/bw16-01/status
vendoghost/bw16-01/scan
vendoghost/all/command
```

Supported commands are:

- `scan` - scan both supported bands and publish the results
- `status` - publish the current device status
- `restart` - restart the module
- `set_interval:<milliseconds>` - set status publishing from 30 seconds to 1 hour
- `stop_attack` - accepted as an ESP32-compatible scanner-safe standby command

Scan payloads retain the ESP32 fields and add `band`, whose value is either
`2.4GHz` or `5GHz`. ArduinoJson 7 grows the scan document dynamically, while
the MQTT packet buffer is capped at 16 KB and oversized payloads are rejected.

## Build

Select `Ai-Thinker BW16 (RTL8720DN)` in Arduino IDE and upload `BW16.ino`, or
compile from the project directory with:

```powershell
arduino-cli compile --fqbn realtek:AmebaD:Ai-Thinker_BW16 .
```

Use this scanner only on networks and radio environments you own or are
authorized to assess.
