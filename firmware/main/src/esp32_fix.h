#pragma once

// Workaround for ESP32 Arduino Core 2.x tcpip_adapter.h bug
// The SDK header uses ip6_addr_t without including the right lwIP header
// This must be included BEFORE any WiFi/networking headers

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare the missing type by including the lwIP header that defines it
#include "lwip/ip_addr.h"

#ifdef __cplusplus
}
#endif
