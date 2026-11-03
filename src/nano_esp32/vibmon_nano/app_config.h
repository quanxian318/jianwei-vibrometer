#pragma once
#include <stddef.h>
#include <stdint.h>

#define VIBMON_NANO_VERSION "0.2.3"
#define VIBMON_NANO_PROFILE "carrier-2026-10-20"

namespace cfg {

// USB CDC console and J605 UART0. Independent of the AX UART1/NVS settings.
constexpr uint32_t kDebugBaud = 115200;

// AX link (protocol V1). Both ends must use the same baud; it is stored in
// NVS and can be changed from the USB console ("baud <rate>", then reboot).
constexpr uint32_t kDefaultLinkBaud = 115200;
constexpr bool kDefaultFlowControl = true;
constexpr uint8_t kRtsThreshold = 64;          // RX FIFO level that deasserts RTS
constexpr size_t kUartRxBuffer = 4096;
constexpr size_t kUartTxBuffer = 4096;
constexpr uint32_t kPartialFrameTimeoutMs = 200;
constexpr size_t kTxQueueBytes = 8192;

// Session bookkeeping.
constexpr size_t kSeqCacheSize = 32;
constexpr size_t kMaxIdLength = 64;

// Events.
constexpr size_t kEventQueueCapacity = 32;
constexpr size_t kEventMaxBytes = 384;

// Audio.
constexpr size_t kAudioQueueMax = 4;
constexpr size_t kJobHistory = 32;
constexpr uint32_t kDefaultSampleRate = 16000;
constexpr int kI2sDmaBufCount = 6;
constexpr int kI2sDmaBufFrames = 256;

// SPI devices sharing D11/D12/D13 (U601 gates MISO per chip select).
constexpr uint32_t kSdSpiHz = 4000000;
constexpr uint32_t kPn532SpiHz = 1000000;

// NFC polling.
constexpr uint32_t kNfcPollIntervalMs = 250;
constexpr uint32_t kNfcRemoveMisses = 2;
constexpr uint32_t kNfcRetryInitMs = 5000;

// SD card detect.
constexpr uint32_t kSdDebounceMs = 300;
constexpr uint32_t kSdRetryMountMs = 5000;

// Power fail input.
constexpr uint32_t kPfailReleaseMs = 1000;

// Phone app gateway: access point "VIBMON-xxxx" (last MAC bytes) at
// 192.168.4.1, HTTP API v1 on port 80, UDP discovery, mDNS vibmon-xxxx.local.
constexpr const char* kDefaultApPassword = "vibmon2026";  // private use; change in the app
constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kDiscoveryPort = 48300;

// Wi-Fi / NTP.
constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint32_t kWifiBackoffMinMs = 5000;
constexpr uint32_t kWifiBackoffMaxMs = 120000;
constexpr const char* kDefaultNtp1 = "ntp.aliyun.com";
constexpr const char* kDefaultNtp2 = "cn.pool.ntp.org";
constexpr const char* kDefaultNtp3 = "pool.ntp.org";
// SNTP in lwIP does not expose the round-trip delay; the reported uncertainty
// is this base value plus 50 ppm of the time since the last sync.
constexpr uint32_t kNtpBaseUncertaintyMs = 100;

}  // namespace cfg

// Voice manifest: asset_id -> file on the carrier MicroSD card (J603).
// Matches 04_NanoESP32开发/voice_assets.csv. Only these ids are playable.
struct VoiceAsset {
  const char* id;
  const char* path;
};

static const VoiceAsset kVoiceAssets[] = {
    {"print_sent", "/voice/print_sent.wav"},
    {"calibration_done", "/voice/calibration_done.wav"},
    {"measure_failed", "/voice/measure_failed.wav"},
    {"measure_start", "/voice/measure_start.wav"},
    {"baseline_failed", "/voice/baseline_failed.wav"},
    {"baseline_start", "/voice/baseline_start.wav"},
    {"welcome", "/voice/welcome.wav"},
    {"baseline_done", "/voice/baseline_done.wav"},
    {"measure_done", "/voice/measure_done.wav"},
    {"vibration_warn", "/voice/vibration_warn.wav"},
    {"vibration_alarm", "/voice/vibration_alarm.wav"},
    {"print_failed", "/voice/print_failed.wav"},
    {"storage_error", "/voice/storage_error.wav"},
};
static const size_t kVoiceAssetCount = sizeof(kVoiceAssets) / sizeof(kVoiceAssets[0]);

inline const VoiceAsset* findVoiceAsset(const char* id) {
  for (size_t i = 0; i < kVoiceAssetCount; ++i) {
    const char* a = kVoiceAssets[i].id;
    const char* b = id;
    while (*a && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == 0 && *b == 0) return &kVoiceAssets[i];
  }
  return nullptr;
}
