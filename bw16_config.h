#pragma once

#include "bw16_secrets.h"

#define MQTT_BROKER "17bd4d19c8574ecca41de5d41d95e99a.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883

// Use a unique ID for every physical device.
#define DEVICE_ID "2"

#define TOPIC_CMD "vendoghost/" DEVICE_ID "/command"
#define TOPIC_CMD_RETAINED "vendoghost/" DEVICE_ID "/command/retained"
#define TOPIC_STATUS "vendoghost/" DEVICE_ID "/status"
#define TOPIC_SCAN "vendoghost/" DEVICE_ID "/scan"
#define TOPIC_BROADCAST "vendoghost/all/command"
