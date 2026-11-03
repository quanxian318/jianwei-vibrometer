#include "sd_service.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "app_config.h"
#include "audio_service.h"
#include "board_pins.h"
#include "event_store.h"
#include "ff.h"
#include "log.h"

SdService g_sd;

namespace {
const char* kTag = "sd";

const char* fatTypeName() {
  for (int drv = 0; drv < FF_VOLUMES; ++drv) {
    char path[3] = {static_cast<char>('0' + drv), ':', 0};
    DWORD freeClusters = 0;
    FATFS* fs = nullptr;
    if (f_getfree(path, &freeClusters, &fs) == FR_OK && fs != nullptr) {
      switch (fs->fs_type) {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
#ifdef FS_EXFAT
        case FS_EXFAT: return "exFAT";
#endif
        default: return "FAT";
      }
    }
  }
  return "unknown";
}

const char* cardTypeName(uint8_t t) {
  switch (t) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    case CARD_NONE: return "none";
    default: return "unknown";
  }
}
}  // namespace

void SdService::begin() {
  pinMode(board_pins::SD_CD, INPUT);  // R610 pulls up; the socket switch grounds it
  rawLast_ = digitalRead(board_pins::SD_CD) == LOW;
  rawSince_ = millis();
}

void SdService::poll() {
  const uint32_t now = millis();
  const bool raw = digitalRead(board_pins::SD_CD) == LOW;
  if (raw != rawLast_) {
    rawLast_ = raw;
    rawSince_ = now;
  }
  if (now - rawSince_ >= cfg::kSdDebounceMs && raw != present_) {
    present_ = raw;
    LOGI(kTag, "card %s", raw ? "inserted" : "removed");
    if (raw) {
      mountFailures_ = 0;
      nextMountTry_ = now;
    } else {
      unmount("removed");
      postState(nullptr);
    }
  }
  if (present_ && !mounted_ && allowMount_ && static_cast<int32_t>(now - nextMountTry_) >= 0) {
    mount();
  }
}

void SdService::mount() {
  if (SD.begin(board_pins::SD_CS, SPI, cfg::kSdSpiHz, "/sd", 5, false)) {
    mounted_ = true;
    mountFailures_ = 0;
    LOGI(kTag, "mounted %s %s, %llu MB", cardTypeName(SD.cardType()), fatTypeName(),
         static_cast<unsigned long long>(SD.totalBytes() >> 20));
    postState(nullptr);
    return;
  }
  SD.end();
  mountFailures_++;
  nextMountTry_ = millis() + cfg::kSdRetryMountMs;
  LOGW(kTag, "mount failed (%lu)", static_cast<unsigned long>(mountFailures_));
  if (mountFailures_ == 1) {
    postState("mount_failed");
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("module", "sd");
    d.set("code", "MOUNT_FAILED");
    g_events.post("FAULT", d);
  }
}

void SdService::unmount(const char* why) {
  // Playback must release its file handle before the volume goes away.
  g_audio.stopAll(strcmp(why, "removed") == 0 ? "sd_removed" : why, strcmp(why, "removed") == 0);
  if (mounted_) {
    SD.end();
    mounted_ = false;
    LOGI(kTag, "unmounted (%s)", why);
  }
}

void SdService::allowMount(bool allow) {
  allowMount_ = allow;
  if (!allow && mounted_) {
    unmount("power_off");
    postState(nullptr);
  }
  if (allow) nextMountTry_ = millis();
}

bool SdService::exists(const char* path) const { return mounted_ && SD.exists(path); }

void SdService::postState(const char* error) {
  vp::JsonValue d = vp::JsonValue::makeObject();
  d.set("present", static_cast<bool>(present_));
  d.set("mounted", static_cast<bool>(mounted_));
  if (error) d.set("error", error);
  if (mounted_) {
    d.set("fs", fatTypeName());
    vp::JsonValue missing = vp::JsonValue::makeArray();
    for (size_t i = 0; i < kVoiceAssetCount; ++i) {
      if (!SD.exists(kVoiceAssets[i].path)) missing.push(kVoiceAssets[i].id);
    }
    d.set("assets_missing", missing);
  }
  g_events.post("SD_STATE", d);
}

void SdService::describeShort(vp::JsonValue& obj) const {
  obj.set("present", static_cast<bool>(present_));
  obj.set("mounted", static_cast<bool>(mounted_));
}

void SdService::describe(vp::JsonValue& obj) const {
  describeShort(obj);
  if (!mounted_) {
    obj.set("fs", vp::JsonValue());
    obj.set("total_bytes", 0);
    obj.set("free_bytes", 0);
    return;
  }
  const uint64_t total = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  obj.set("fs", fatTypeName());
  obj.set("card_type", cardTypeName(SD.cardType()));
  obj.set("total_bytes", static_cast<unsigned long long>(total));
  obj.set("free_bytes", static_cast<unsigned long long>(total > used ? total - used : 0));
  vp::JsonValue assets = vp::JsonValue::makeObject();
  for (size_t i = 0; i < kVoiceAssetCount; ++i) {
    assets.set(kVoiceAssets[i].id, SD.exists(kVoiceAssets[i].path));
  }
  obj.set("assets", assets);
}
