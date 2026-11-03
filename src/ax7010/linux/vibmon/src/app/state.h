#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "drv/ina226.h"

namespace vm {

enum class AlarmLevel { kUnknown = -1, kNormal = 0, kWarn = 1, kAlarm = 2 };

struct ResultSummary {
  bool have = false;
  bool valid = false;
  std::string measurementId;
  std::string reason;
  std::string unit = "px";  // mm when calibrated
  double dominantHz = 0, peakToPeak = 0, rms = 0, peak = 0;
  bool peakClear = true;     // a clear spectral line; else dominantHz is only the largest bin
  double fps = 0;
  std::string method = "roi";
  int points = 0, passedPoints = 0;  // multipoint: grid points, points above the baseline
  double pointRmsPx = 0;             // multipoint: median RMS of the points above the baseline
  bool waveformValid = true;         // multipoint: the representative waveform passed its checks
  std::string waveformReason;        // multipoint: why it did not
  uint32_t dropped = 0;
  AlarmLevel level = AlarmLevel::kUnknown;
  bool saved = false;
  std::string pointId;
  int64_t utcMs = 0;
  // Acoustic reference of this measurement (enabled = configured; reason
  // when the audio was not available or not valid).
  bool acousticEnabled = false, acousticValid = false;
  std::string acousticReason, acousticRelation;  // relation: agree | harmonic | differ | no_visual | no_peak
  double acousticHz = 0;
  int acousticHarmonic = 0;
};

// Operator settings (settings page / "set" command), persisted to
// AppConfig::settingsFile.
struct UserSettings {
  double durationS = 10;
  int roiW = 64, roiH = 64;  // measuring box (pixels)
  int roiMaxPx = 256;        // largest box side the configured tracker takes (not a setting)
  char axis = 'y';
  bool autoLight = true;
  double warnPp = 2.0, alarmPp = 5.0;  // mm
  bool binaryThreshold = false;
  bool buzzer = true;
  bool voice = true;
  int volume = 40;
  std::string fanMode = "auto";
  int ledBrightness = 32;  // 0..255
  double visionLowHz = 2, visionHighHz = 5;
  int visionGain = 20;
  bool laserOffDuringMeasure = true;
};

// Everything the UI, LEDs and control socket show. Written by services,
// read as a snapshot copy.
struct SystemState {
  bool initDone = false;
  bool carrierOk = false, tcaOk = false, inaOk = false, rtcOk = false;
  std::string fault;  // non-empty = system fault (HL101 red)

  PowerSample power;
  bool powerWarn = false, powerDeny = false, overCurrentLatched = false;
  bool pfail = false;

  bool lightAllowed = false;  // LIGHT_OK (SW301 closed): permits both LED1 and LED2
  bool lightOn = false;       // LED1 (J301): all LED lamps
  double lightPercent = 100;  // LED1 PWM duty
  bool laserOn = false;       // LED2 (J304): 12 V 650 nm cross-line projector, on/off only

  bool timeValid = false;
  std::string timeSource = "none";

  bool nanoOnline = false;
  std::string wifiState = "UNKNOWN", wifiSsid, wifiIp;
  bool sdMounted = false, nfcReady = false;

  std::string pointUid, pointId, pointName;

  bool imuOk = false, adxlOk = false, tofOk = false, shtOk = false;
  std::string tofNote;
  double tempC = 0, rhPercent = 0;
  bool envValid = false;
  int distanceMm = 0;
  bool distanceValid = false;
  double tachHz = 0;  // J705 pulse frequency (pulses per second, before PPR)
  bool tachValid = false;

  std::string printerState = "idle";
  std::string printerError;

  std::string measureState = "stopped";  // stopped | preview | capturing | analyzing
  double measureElapsedS = 0, measureDurationS = 0;
  bool cameraOk = false;
  ResultSummary last;

  AlarmLevel alarmLevel = AlarmLevel::kUnknown;
  bool alarmMuted = false;

  double zoom = 1.0;
  double zoomCx = 0.5, zoomCy = 0.5;
  double roiCx = 0.5, roiCy = 0.5;
  // Multi-point measurement (measurement.method "multipoint").
  std::string method = "roi";
  double refCx = 0.1, refCy = 0.9;  // centre of the reference group (normalised)
  bool baselineValid = false;       // a baseline exists for the current layout
  int baselineWindows = 0;
  std::string layoutError;          // the box or the references cannot be used
  bool baselineCapture = false;     // the running capture collects a baseline

  std::string message;  // latest user-facing message
  int64_t messageMs = 0;
  bool shutdownPrepared = false;

  UserSettings settings;
  bool calibrated = false;
  std::string calibrationId;
  std::string deviceId, appVersion, plVersion, nanoVersion, cameraSource;
  bool imuEnabled = false, adxlEnabled = false, tofEnabled = false, shtEnabled = false, printerEnabled = false;
  bool fanOn = false;
  // MP34DT01 acoustic reference: disabled | starting | warmup | ready | capturing | fault.
  std::string acousticState = "disabled", acousticError, acousticLr;
  double acousticLevelDbfs = -200, acousticRate = 0;
  double powerWarnA = 0, powerDenyA = 0;
  unsigned long nanoRequests = 0, nanoRetries = 0, nanoTimeouts = 0, nanoCrcErrors = 0;
};

class StateStore {
 public:
  SystemState snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return s_;
  }
  void update(const std::function<void(SystemState&)>& fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    fn(s_);
  }

 private:
  std::mutex mutex_;
  SystemState s_;
};

}  // namespace vm
