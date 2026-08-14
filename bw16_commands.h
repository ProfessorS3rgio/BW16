#pragma once

#include <Arduino.h>

enum class Bw16CommandType {
  Empty,
  Scan,
  Status,
  Restart,
  Stop,
  SetInterval,
  Unsupported
};

struct Bw16Command {
  Bw16CommandType type;
  unsigned long intervalMs;
};

inline Bw16Command parseBw16Command(String command) {
  command.trim();
  if (command.length() == 0) return {Bw16CommandType::Empty, 0};
  if (command == "scan") return {Bw16CommandType::Scan, 0};
  if (command == "status") return {Bw16CommandType::Status, 0};
  if (command == "restart") return {Bw16CommandType::Restart, 0};
  if (command == "stop_attack") return {Bw16CommandType::Stop, 0};

  if (command.startsWith("set_interval:")) {
    const unsigned long interval = command.substring(13).toInt();
    return {Bw16CommandType::SetInterval, interval};
  }

  return {Bw16CommandType::Unsupported, 0};
}
