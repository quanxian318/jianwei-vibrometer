#pragma once

// Line commands on USB CDC or J605 UART0 (115200 8N1), without the AX7010:
//   help | ping | status | events | sd | nfc | play <asset_id> [volume] | stop
//   wifi <ssid> [password] | wifi off | baud <rate> | flow on|off
//   log error|warn|info|debug | reboot
// Replies and logs are mirrored to both ports. The Wi-Fi password is never echoed.
namespace console {
void poll();
}
