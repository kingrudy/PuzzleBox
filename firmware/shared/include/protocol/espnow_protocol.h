#pragma once

#include <cstdint>

// ESP-NOW wire protocol between the main controller and the C3 sidecar.
// Unencrypted, single-hop. See spec/puzzlebox_hw.md section 7.2.
//
// Payloads are hard-capped at kMaxPayloadBytes. Adding a field to a payload
// struct is a breaking wire change — bump kProtocolVersion if you do.

namespace protocol {

inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::size_t kMaxPayloadBytes = 32;

enum class MessageType : std::uint8_t {
  Hello = 0,
  HelloAck,
  Heartbeat,
  StateSnapshot,
  SceneSet,
  TimerSet,
  PuzzleSet,
  HintLevelSet,
  DisplayTextShort,
  NodeTest,
  ResetSideEffects,
  LocalInputEvent,
  LocalSensorEvent,
  NodeFault,
  DiagStatus,
  Ack,
};

#pragma pack(push, 1)
struct PacketEnvelope {
  std::uint8_t protocolVersion;
  std::uint8_t messageType;   // MessageType
  std::uint16_t sequence;
  std::uint32_t senderId;     // low 4 bytes of the sender's MAC
  std::uint16_t payloadLength;  // <= kMaxPayloadBytes
  std::uint16_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(PacketEnvelope) == 12, "PacketEnvelope must stay 12 bytes on the wire");

struct Packet {
  PacketEnvelope envelope;
  std::uint8_t payload[kMaxPayloadBytes];
};

// Handshake/liveness timing (main <-> sidecar). See section 7.2.
inline constexpr std::uint32_t kHelloIntervalMs = 1500;
inline constexpr std::uint32_t kHeartbeatIntervalMs = 1000;
inline constexpr std::uint32_t kPeerLostTimeoutMs = 10000;

inline constexpr std::uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace protocol
