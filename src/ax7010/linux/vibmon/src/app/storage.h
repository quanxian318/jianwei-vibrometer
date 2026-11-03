#pragma once

#include <string>
#include <vector>

#include "app/sensor_service.h"
#include "app/state.h"
#include "measure/acoustic.h"
#include "measure/session.h"
#include "util/json_util.h"

namespace vm {

struct RecordMeta {
  std::string measurementId;
  std::string deviceId;
  std::string pointId, pointName, nfcUid;
  int64_t startUtcMs = 0;
  bool timeValid = false;
  std::string timeSource;
  int tzOffsetMin = 0;
  std::string cameraSource;
  double lightPercent = 0;
  bool lightOn = false;
  bool laserOn = false;             // laser speckle projector during the capture
  bool laserOffForCapture = false;  // it was on and was switched off for this capture
  std::string unit;  // mm | px
  AlarmLevel level = AlarmLevel::kUnknown;
  double warnPp = 0, alarmPp = 0;
  bool envValid = false;
  double tempC = 0, rhPercent = 0;
  bool distanceValid = false;
  int distanceMm = 0;
  std::string appVersion, nanoVersion, plVersion;
  // Stereo depth at the ROI when the capture started (stereo.enabled only).
  struct Stereo {
    bool enabled = false;
    std::string backend, calibrationId;
    bool roiDepthValid = false;
    double roiDepthMm = 0;
    bool usedForScale = false;
  } stereo;
  // MP34DT01 acoustic reference over the capture window (acoustic.enabled only).
  struct Acoustic {
    bool enabled = false;
    bool available = false;  // a complete, continuous window was captured
    std::string reason;      // why not available (not_ready | not_buffered | gap | late | the service fault)
    std::string source, lr;
    double sampleRate = 0, pdmClockHz = 0;
    uint64_t firstIndex = 0;
    double startOffsetMs = 0;  // first sample relative to the first video frame
    std::vector<int16_t> pcm;  // written as acoustic.wav when saveWav
    bool saveWav = true;
    AcousticAnalysis analysis;
    double bandLowHz = 0, bandHighHz = 0;
    bool fanOn = false, buzzer = false;
    int voiceSuppressed = 0;  // prompts held back during the capture
  } acoustic;
};

// <records>/<measurement_id>/record.json (schema vision-vibration-record-1,
// matching measurement_record.example.json) plus waveform.csv, spectrum.csv
// and, when available, imu.csv / reference_adxl345.csv, acoustic.wav and
// acoustic_spectrum.csv.
bool saveRecord(const std::string& dir, const RecordMeta& meta, const MeasureResult& r,
                const std::vector<ImuSample>& imu, const std::vector<AdxlSample>& adxl, std::string& err);
// Loads the parts of a saved record needed to print it.
bool loadRecord(const std::string& dir, const std::string& id, JsonValue& record, std::vector<double>& t,
                std::vector<double>& disp, std::vector<double>& f, std::vector<double>& amp);
// Envelope spectrum of the acoustic reference (empty when the record has none).
void loadAcousticSpectrum(const std::string& dir, const std::string& id, std::vector<double>& f,
                          std::vector<double>& amp);
// Acoustic figures of a record into s (acoustic* fields).
void readAcousticSummary(const JsonValue& record, ResultSummary& s);
std::string latestRecordId(const std::string& dir);

// One line of the record list (history page).
// Method and multi-point figures of a record's "result" object into s
// (points, points above the baseline, their median RMS).
void readMultiPointSummary(const JsonValue& result, ResultSummary& s);

struct RecordSummary {
  std::string id, startLocal, pointId, pointName, reason, unit;
  bool valid = false;
  bool peakClear = true;   // false: dominantHz is only the largest bin
  bool amplitudes = true;  // false: peak-to-peak and RMS unknown (multi-point waveform invalid)
  double dominantHz = 0, peakToPeak = 0, rms = 0;
  AlarmLevel level = AlarmLevel::kUnknown;
};
// Newest first (record.json modification time), at most maxCount entries.
std::vector<RecordSummary> listRecords(const std::string& dir, size_t maxCount);

}  // namespace vm
