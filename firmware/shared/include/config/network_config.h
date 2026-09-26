#pragma once

// Network identities shared by every node. See spec/puzzlebox_hw.md section 7.1.
//
// The Wi-Fi AP channel and the ESP-NOW channel are the SAME constant on
// purpose: an ESP32 has one radio, and ESP-NOW peers must sit on the AP's
// channel. Never hardcode a different channel anywhere else.

namespace config {

inline constexpr char kApSsid[] = "Chronolab-X13";
inline constexpr char kApPassword[] = "chronolab13";
inline constexpr int kAccessPointChannel = 6;

inline constexpr char kOtaPassword[] = "chronolab-ota";
inline constexpr int kOtaPort = 3232;

inline constexpr char kMainControllerIp[] = "192.168.4.1";
inline constexpr char kMainControllerHostname[] = "chronolab-main";

inline constexpr char kSidecarIp[] = "192.168.4.210";
inline constexpr char kSidecarHostname[] = "chronolab-sidecar";

inline constexpr char kMainDisplayIp[] = "192.168.4.211";
inline constexpr char kMainDisplayHostname[] = "chronolab-display-main";

inline constexpr char kRoundDisplayIp[] = "192.168.4.212";
inline constexpr char kRoundDisplayHostname[] = "chronolab-display-round";

inline constexpr int kHttpPort = 80;
inline constexpr int kWebSocketPort = 81;

}  // namespace config
