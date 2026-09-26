#pragma once

// Compatibility shim for WebServer in ESP32 Arduino Core 3.x
// The bundled WebServer.h calls WiFiClient.write_P() which was removed in Core 3.x
// This header must be included BEFORE WebServer.h

#include <cstddef>
#include <cstdint>
#include <WiFiClient.h>

namespace webserver_compat {
  // Extension to WiFiClient that adds back write_P method
  class WiFiClientCompatImpl : public WiFiClient {
  public:
    using WiFiClient::WiFiClient;
    
    // Add back write_P method that was removed in Core 3.x
    size_t write_P(const char* buf, size_t size) {
      return write(reinterpret_cast<const uint8_t*>(buf), size);
    }
  };
}

// Replace WiFiClient with our compatible version
#define WiFiClient webserver_compat::WiFiClientCompatImpl
