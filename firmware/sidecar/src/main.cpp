#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#include <cstring>

#include "app/sidecar_controller.h"
#include "config/network_config.h"
#include "protocol/espnow_protocol.h"

namespace {

SidecarController sidecarController;
bool peerRegistered = false;

void onDataReceived(const std::uint8_t* mac, const std::uint8_t* data, int len) {
  if (len < static_cast<int>(sizeof(protocol::PacketEnvelope))) {
    return;
  }
  protocol::PacketEnvelope envelope;
  memcpy(&envelope, data, sizeof(envelope));

  const auto type = static_cast<protocol::MessageType>(envelope.messageType);
  if (type == protocol::MessageType::HelloAck) {
    if (!peerRegistered) {
      esp_now_peer_info_t peer{};
      memcpy(peer.peer_addr, mac, 6);
      peer.channel = config::kAccessPointChannel;
      peer.encrypt = false;
      esp_now_add_peer(&peer);
      peerRegistered = true;
    }
    sidecarController.onControllerHeartbeat();
  } else if (type == protocol::MessageType::Heartbeat) {
    sidecarController.onControllerHeartbeat();
  }
}

}  // namespace

void setup() {
  sidecarController.begin();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    sidecarController.eventLog().logf("sidecar", "esp_now_init failed");
    return;
  }
  esp_now_register_recv_cb(onDataReceived);
}

void loop() { sidecarController.tick(); }
