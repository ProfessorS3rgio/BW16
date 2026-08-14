#ifndef PACKET_INJECTION_H
#define PACKET_INJECTION_H

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

// 802.11 frame types
#define FRAME_TYPE_MANAGEMENT 0x00
#define FRAME_SUBTYPE_DEAUTH 0x0C
#define FRAME_SUBTYPE_DISASSOC 0x0A
#define FRAME_SUBTYPE_BEACON 0x08
#define FRAME_SUBTYPE_PROBE_REQUEST 0x04

// Reason codes
#define REASON_UNSPECIFIED 0x01
#define REASON_AUTH_EXPIRE 0x02
#define REASON_DEAUTH_LEAVING 0x03
#define REASON_DISASSOC_INACTIVITY 0x04
#define REASON_DISASSOC_AP_BUSY 0x05
#define REASON_CLASS2_FRAME 0x06
#define REASON_CLASS3_FRAME 0x07
#define REASON_DISASSOC_STA_LEAVING 0x08
#define REASON_STA_REQ_ASSOC 0x09

// 802.11 frame structures
typedef struct {
  uint16_t frame_control;
  uint16_t duration;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  uint16_t sequence_number;
  uint16_t reason;
} DeauthFrame;

typedef struct {
  uint16_t frame_control;
  uint16_t duration;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  uint16_t sequence_number;
  uint16_t reason;
} DisassocFrame;

typedef struct {
  uint16_t frame_control;
  uint16_t duration;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  uint16_t sequence_number;
  uint64_t timestamp;
  uint16_t beacon_interval;
  uint16_t ap_capabilities;
  uint8_t ssid_tag;
  uint8_t ssid_length;
  uint8_t ssid[255];
} BeaconFrame;

typedef struct {
  uint16_t frame_control;
  uint16_t duration;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  uint16_t sequence_number;
  uint8_t ssid_tag;
  uint8_t ssid_length;
  uint8_t ssid[255];
  uint8_t supported_rates_tag;
  uint8_t supported_rates_length;
  uint8_t supported_rates[8];
} ProbeRequestFrame;

/*
 * Import the needed c functions from the closed-source libraries
 */
extern "C" {
  extern uint8_t* rltk_wlan_info;
  void* alloc_mgtxmitframe(void* ptr);
  void update_mgntframe_attrib(void* ptr, void* frame_control);
  int dump_mgntframe(void* ptr, void* frame_control);
}

// Function declarations
void wifi_tx_raw_frame(void* frame, size_t length);
void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason = 0x05);  // Default to reason 5
void wifi_tx_disassoc_frame(void* src_mac, void* dst_mac, uint16_t reason = 0x05);
void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid);
void wifi_tx_probe_request_frame(void* src_mac, const char *ssid);

#endif