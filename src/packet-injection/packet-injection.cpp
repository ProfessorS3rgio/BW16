#include "packet-injection.h"
#include <Arduino.h>

/*
 * Transmits a raw 802.11 frame with a given length.
 */
void wifi_tx_raw_frame(void* frame, size_t length) {
  uint8_t *ptr = (uint8_t *)**(uint32_t **)(rltk_wlan_info + 0x10);
  uint8_t *frame_control = (uint8_t *)alloc_mgtxmitframe(ptr + 0xa80);

  if (frame_control != 0) {
    update_mgntframe_attrib(ptr, frame_control + 8);
    memset((void *)*(uint32_t *)(frame_control + 0x80), 0, 0x68);
    uint8_t *frame_data = (uint8_t *)*(uint32_t *)(frame_control + 0x80) + 0x28;
    memcpy(frame_data, frame, length);
    *(uint32_t *)(frame_control + 0x14) = length;
    *(uint32_t *)(frame_control + 0x18) = length;
    dump_mgntframe(ptr, frame_control);
  }
}

/*
 * Transmits a 802.11 deauth frame on the active channel
 */
void wifi_tx_deauth_frame(void* src_mac, void* dst_mac, uint16_t reason) {
  DeauthFrame frame;
  
  // Initialize the frame with default values
  memset(&frame, 0, sizeof(DeauthFrame));
  frame.frame_control = 0xC0;  // Deauth frame (Management, subtype 12)
  frame.duration = 0xFFFF;
  frame.sequence_number = 0;
  frame.reason = reason;
  
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  
  wifi_tx_raw_frame(&frame, sizeof(DeauthFrame));
}

/*
 * Transmits a 802.11 disassociation frame on the active channel
 */
void wifi_tx_disassoc_frame(void* src_mac, void* dst_mac, uint16_t reason) {
  DisassocFrame frame;
  
  // Initialize the frame with default values
  memset(&frame, 0, sizeof(DisassocFrame));
  frame.frame_control = 0xA0;  // Disassoc frame (Management, subtype 10)
  frame.duration = 0xFFFF;
  frame.sequence_number = 0;
  frame.reason = reason;
  
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  
  wifi_tx_raw_frame(&frame, sizeof(DisassocFrame));
}

/*
 * Transmits a very basic 802.11 beacon with the given ssid on the active channel
 */
void wifi_tx_beacon_frame(void* src_mac, void* dst_mac, const char *ssid) {
  BeaconFrame frame;
  
  // Initialize the frame with default values
  memset(&frame, 0, sizeof(BeaconFrame));
  frame.frame_control = 0x80;  // Beacon frame (Management, subtype 8)
  frame.duration = 0;
  frame.sequence_number = 0;
  frame.timestamp = 0;
  frame.beacon_interval = 0x64;  // 100 TU = 102.4 ms
  frame.ap_capabilities = 0x21;  // ESS + Short Preamble
  frame.ssid_tag = 0;  // SSID parameter set
  frame.ssid_length = 0;
  
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, src_mac, 6);
  memcpy(&frame.destination, dst_mac, 6);
  
  // Copy SSID
  int ssid_len = strlen(ssid);
  if (ssid_len > 255) ssid_len = 255;
  memcpy(frame.ssid, ssid, ssid_len);
  frame.ssid_length = ssid_len;
  
  wifi_tx_raw_frame(&frame, 38 + frame.ssid_length);
}

/*
 * Transmits a 802.11 probe request frame
 */
void wifi_tx_probe_request_frame(void* src_mac, const char *ssid) {
  ProbeRequestFrame frame;
  
  // Initialize the frame with default values
  memset(&frame, 0, sizeof(ProbeRequestFrame));
  frame.frame_control = 0x40;  // Probe request (Management, subtype 4)
  frame.duration = 0;
  frame.sequence_number = 0;
  
  // Set destination to broadcast
  uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(&frame.destination, broadcast, 6);
  memcpy(&frame.source, src_mac, 6);
  memcpy(&frame.access_point, broadcast, 6);
  
  // Add SSID element
  frame.ssid_tag = 0;  // SSID parameter set
  frame.ssid_length = 0;  // Empty SSID for broadcast probe
  
  // If SSID provided, include it
  if (ssid != nullptr && strlen(ssid) > 0) {
    int ssid_len = strlen(ssid);
    if (ssid_len > 255) ssid_len = 255;
    memcpy(frame.ssid, ssid, ssid_len);
    frame.ssid_length = ssid_len;
  }
  
  // Add supported rates element
  frame.supported_rates_tag = 1;  // Supported rates
  frame.supported_rates_length = 8;
  uint8_t rates[] = {0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24};  // 1, 2, 5.5, 11, 6, 9, 12, 18 Mbps
  memcpy(frame.supported_rates, rates, 8);
  
  size_t frame_size = sizeof(ProbeRequestFrame) - sizeof(frame.supported_rates) + frame.supported_rates_length;
  if (frame.ssid_length > 0) {
    frame_size = 24 + 2 + frame.ssid_length + 2 + frame.supported_rates_length;  // Fixed header + SSID element + rates element
  } else {
    frame_size = 24 + 2 + frame.supported_rates_length;  // Fixed header + rates element (no SSID)
  }
  
  wifi_tx_raw_frame(&frame, frame_size);
}