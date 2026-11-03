#pragma once
#include <Arduino.h>

// Arduino side of the phone app gateway (app_gateway.h): WebServer on
// cfg::kHttpPort, UDP discovery on cfg::kDiscoveryPort and mDNS
// "vibmon-xxxx.local" (_vibmon._tcp). Runs from loop().
namespace http_gateway {

void begin();
void poll();

}  // namespace http_gateway
