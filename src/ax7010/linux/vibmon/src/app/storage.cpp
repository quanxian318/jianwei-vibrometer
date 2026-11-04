#include "app/storage.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "app/report.h"
#include "measure/pdm_model.h"
#include "util/clock.h"
#include "util/wav.h"

namespace vm {

namespace {

JsonValue numOrNull(double v, bool ok) { return ok && std::isfinite(v) ? JsonValue(v) : JsonValue(); }

std::string fmt(double v) {
  char b[32];
  snprintf(b, sizeof(b), "%.6g", v);
  return b;
}

const char* levelKey(AlarmLevel l) {
  if (l == AlarmLevel::kUnknown) return "unknown";
  return l == AlarmLevel::kAlarm ? "alarm" : (l == AlarmLevel::kWarn ? "warn" : "normal");
}

}  // namespace

namespace {

JsonValue peaksJson(const std::vector<AcousticPeak>& peaks) {
  JsonValue a = JsonValue::makeArray();
  for (const AcousticPeak& p : peaks) {
    JsonValue o = JsonValue::makeObject();
    o.set("hz", p.hz);
    o.set("prominence_db", p.prominenceDb);
    a.push(o);
  }
  return a;
}

// The record's acoustic section and files.
bool saveAcoustic(const std::string& path, const RecordMeta::Acoustic& a, JsonValue& rec) {
  JsonValue o = JsonValue::makeObject();
  const AcousticAnalysis& an = a.analysis;
  o.set("model", "MP34DT01");
  o.set("available", a.available);
  o.set("valid", a.available && an.valid);
  o.set("reason", !a.available ? JsonValue(a.reason) : (an.valid ? JsonValue() : JsonValue(an.reason)));
  o.set("source", a.source);
  o.set("lr", a.lr.empty() ? JsonValue() : JsonValue(a.lr));
  o.set("pdm_clock_hz", numOrNull(a.pdmClockHz, a.pdmClockHz > 0));
  o.set("sample_rate_hz", numOrNull(a.sampleRate, a.sampleRate > 0));
  JsonValue flt = JsonValue::makeObject();
  flt.set("chain", "CIC order 5 /32, 63-tap FIR /2, 16-bit PCM (PDM full scale = +/-32768)");
  flt.set("delay_ms", kPdmFilterDelayS * 1e3);
  flt.set("times", "sample times are at the microphone: production time minus this delay");
  o.set("filter", flt);
  bool ok = true;
  if (a.available) {
    JsonValue w = JsonValue::makeObject();
    w.set("first_index", static_cast<unsigned long long>(a.firstIndex));
    w.set("samples", static_cast<unsigned long>(a.pcm.size()));
    w.set("start_offset_ms", a.startOffsetMs);
    w.set("definition", "the video capture window; offset of the first sample from the first frame");
    o.set("window", w);
    JsonValue lv = JsonValue::makeObject();
    lv.set("rms_dbfs", an.rmsDbfs);
    lv.set("peak_dbfs", an.peakDbfs);
    lv.set("dc_fraction", an.dcFraction);
    lv.set("clip_fraction", an.clipFraction);
    lv.set("unit", "dBFS, full-scale sine = 0 dBFS; not a sound pressure level");
    o.set("level", lv);
    if (an.valid) {
      JsonValue band = JsonValue::makeArray();
      band.push(a.bandLowHz);
      band.push(a.bandHighHz);
      o.set("analysis_band_hz", band);
      JsonValue carrier = JsonValue::makeArray();
      carrier.push(an.carrierLowHz);
      carrier.push(an.carrierHighHz);
      o.set("carrier_band_hz", carrier);
      JsonValue bands = JsonValue::makeArray();
      for (const AcousticAnalysis::Band& b : an.bands) {
        JsonValue e = JsonValue::makeObject();
        e.set("low_hz", b.lowHz);
        e.set("high_hz", b.highHz);
        e.set("modulation", b.modulation);
        e.set("peak_hz", numOrNull(b.top.hz, b.top.hz > 0));
        e.set("prominence_db", numOrNull(b.top.prominenceDb, b.top.hz > 0));
        bands.push(e);
      }
      o.set("carrier_bands", bands);
      o.set("envelope_modulation", an.modulation);
      o.set("envelope_peaks", peaksJson(an.envelopePeaks));
      o.set("direct_peaks", peaksJson(an.directPeaks));
      JsonValue tone = JsonValue::makeObject();
      tone.set("hz", numOrNull(an.tone.hz, an.tone.hz > 0));
      tone.set("prominence_db", numOrNull(an.tone.prominenceDb, an.tone.hz > 0));
      o.set("tone", tone);
      JsonValue vis = JsonValue::makeObject();
      vis.set("relation", an.relation);
      vis.set("visual_hz", numOrNull(an.visualHz, an.visualHz > 0));
      vis.set("acoustic_hz", numOrNull(an.acousticHz, an.acousticHz > 0));
      vis.set("difference_hz", numOrNull(an.diffHz, an.relation == "agree" || an.relation == "harmonic" || an.relation == "differ"));
      vis.set("tolerance_hz", numOrNull(an.toleranceHz, an.toleranceHz > 0));
      vis.set("harmonic", an.harmonic);
      vis.set("definition", "envelope peak against the camera's dominant frequency, estimated independently");
      o.set("visual", vis);
      if (!an.envFreq.empty()) {
        std::ostringstream s;
        s << "frequency_hz,envelope_relative\n";
        for (size_t i = 0; i < an.envFreq.size(); ++i) s << fmt(an.envFreq[i]) << ',' << fmt(an.envAmp[i]) << '\n';
        ok = writeFileAtomic(path + "/acoustic_spectrum.csv", s.str()) && ok;
        o.set("spectrum_file", "acoustic_spectrum.csv");
      }
    }
    if (a.saveWav && !a.pcm.empty() && a.sampleRate > 0) {
      ok = writeFileAtomic(path + "/acoustic.wav",
                           wavMono16(a.pcm.data(), a.pcm.size(), static_cast<int>(std::lround(a.sampleRate)))) && ok;
      o.set("wav_file", "acoustic.wav");
      o.set("wav_rate_note", "WAV header rate is the measured rate rounded to 1 Hz; use sample_rate_hz");
    }
  }
  JsonValue cond = JsonValue::makeObject();
  cond.set("fan_on", a.fanOn);
  cond.set("buzzer", a.buzzer);
  cond.set("voice_prompts_held", a.voiceSuppressed);
  o.set("conditions", cond);
  o.set("note", "acoustic corroboration of the frequency only: no displacement, velocity or sound pressure level");
  rec.set("acoustic", o);
  return ok;
}

}  // namespace

void readAcousticSummary(const JsonValue& record, ResultSummary& s) {
  const JsonValue* a = record.find("acoustic");
  s.acousticEnabled = a != nullptr;
  if (!a) return;
  s.acousticValid = jBool(*a, "valid", false);
  s.acousticReason = jStr(*a, "reason", "");
  const JsonValue& v = jObj(*a, "visual");
  s.acousticRelation = jStr(v, "relation", "");
  s.acousticHz = jNum(v, "acoustic_hz", 0);
  s.acousticHarmonic = static_cast<int>(jInt(v, "harmonic", 0));
}

void readMultiPointSummary(const JsonValue& result, ResultSummary& s) {
  s.method = jStr(result, "method", "roi");
  s.points = s.passedPoints = 0;
  s.pointRmsPx = 0;
  s.waveformValid = true;
  s.waveformReason.clear();
  if (s.method != "multipoint") return;
  const JsonValue& mp = jObj(result, "multipoint");
  s.passedPoints = static_cast<int>(jInt(mp, "passed_points", 0));
  s.waveformValid = jBool(mp, "waveform_valid", true);
  s.waveformReason = jStr(mp, "waveform_reason", "");
  std::vector<double> rms;
  if (const JsonValue* pts = mp.find("points")) {
    for (const JsonValue& p : pts->items()) {
      ++s.points;
      if (jBool(p, "above_baseline", false)) rms.push_back(jNum(p, "rms_px", 0));
    }
  }
  std::sort(rms.begin(), rms.end());
  const size_t n = rms.size();
  if (n) s.pointRmsPx = n % 2 ? rms[n / 2] : 0.5 * (rms[n / 2 - 1] + rms[n / 2]);
}

bool saveRecord(const std::string& dir, const RecordMeta& m, const MeasureResult& r,
                const std::vector<ImuSample>& imu, const std::vector<AdxlSample>& adxl, std::string& err) {
  const std::string path = dir + "/" + m.measurementId;
  if (!makeDirs(path)) {
    err = "mkdir " + path;
    return false;
  }
  const bool cal = r.mmPerPx > 0;
  const double k = cal ? r.mmPerPx : 1.0;

  // waveform.csv
  {
    std::ostringstream w;
    const char other = r.axis == 'x' ? 'y' : 'x';
    w << "t_s,disp_" << r.axis << "_px,disp_" << other << "_px" << (cal ? std::string(",disp_") + r.axis + "_mm" : "")
      << "\n";
    for (size_t i = 0; i < r.t.size(); ++i) {
      w << fmt(r.t[i]) << ',' << fmt(r.disp[i]) << ',' << fmt(i < r.dispOther.size() ? r.dispOther[i] : 0);
      if (cal) w << ',' << fmt(r.disp[i] * k);
      w << '\n';
    }
    if (!writeFileAtomic(path + "/waveform.csv", w.str())) {
      err = "write waveform.csv";
      return false;
    }
  }
  // spectrum.csv
  {
    std::ostringstream s;
    s << "f_hz,amp_px" << (cal ? ",amp_mm" : "") << "\n";
    for (size_t i = 0; i < r.spectrum.freq.size(); ++i) {
      s << fmt(r.spectrum.freq[i]) << ',' << fmt(r.spectrum.amp[i]);
      if (cal) s << ',' << fmt(r.spectrum.amp[i] * k);
      s << '\n';
    }
    if (!writeFileAtomic(path + "/spectrum.csv", s.str())) {
      err = "write spectrum.csv";
      return false;
    }
  }
  if (!imu.empty()) {
    std::ostringstream s;
    s << "t_s,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c\n";
    for (const auto& v : imu) {
      s << fmt((v.monoNs - r.startMonoNs) * 1e-9) << ',' << fmt(v.ax) << ',' << fmt(v.ay) << ',' << fmt(v.az) << ','
        << fmt(v.gx) << ',' << fmt(v.gy) << ',' << fmt(v.gz) << ',' << fmt(v.tempC) << '\n';
    }
    writeFileAtomic(path + "/imu.csv", s.str());
  }
  if (!adxl.empty()) {
    std::ostringstream s;
    s << "t_s,x_g,y_g,z_g\n";
    for (const auto& v : adxl) {
      s << fmt((v.monoNs - r.startMonoNs) * 1e-9) << ',' << fmt(v.x) << ',' << fmt(v.y) << ',' << fmt(v.z) << '\n';
    }
    writeFileAtomic(path + "/reference_adxl345.csv", s.str());
  }

  JsonValue rec = JsonValue::makeObject();
  rec.set("schema", "vision-vibration-record-1");
  rec.set("measurement_id", m.measurementId);
  rec.set("device_id", m.deviceId);
  rec.set("point_id", m.pointId.empty() ? JsonValue() : JsonValue(m.pointId));
  rec.set("point_name", m.pointName.empty() ? JsonValue() : JsonValue(m.pointName));
  rec.set("nfc_uid_hex", m.nfcUid.empty() ? JsonValue() : JsonValue(m.nfcUid));
  rec.set("start_utc", m.timeValid ? JsonValue(formatUtcIso(m.startUtcMs)) : JsonValue());
  rec.set("start_local",
          m.timeValid ? JsonValue(formatLocal(m.startUtcMs, m.tzOffsetMin, "%Y-%m-%d %H:%M:%S")) : JsonValue());
  rec.set("time_valid", m.timeValid);
  rec.set("time_source", m.timeSource);
  rec.set("timezone_offset_min", m.tzOffsetMin);
  JsonValue mono = JsonValue::makeObject();
  mono.set("source", "CLOCK_MONOTONIC");
  mono.set("unit", "ns");
  mono.set("start", static_cast<long long>(r.startMonoNs));
  rec.set("monotonic_clock", mono);

  JsonValue cam = JsonValue::makeObject();
  cam.set("source", m.cameraSource);
  cam.set("width", r.frameWidth);
  cam.set("height", r.frameHeight);
  cam.set("fps_nominal", r.fpsNominal);
  cam.set("fps_measured", numOrNull(r.fpsMeasured, r.fpsMeasured > 0));
  cam.set("fps_received", numOrNull(r.durationS > 0 && r.frames > 1 ? (r.frames - 1) / r.durationS : 0, r.durationS > 0));
  cam.set("drop_fraction", r.frames + r.dropped ? double(r.dropped) / (r.frames + r.dropped) : 0);
  cam.set("gap_resampled", r.dropped > 0 && (r.method == "multipoint" ? r.multipoint.waveformValid : r.valid));
  cam.set("frame_interval_jitter_ms", r.jitterMs);
  cam.set("exposure_us", JsonValue());
  cam.set("frames", static_cast<unsigned long>(r.frames));
  cam.set("frames_dropped", static_cast<unsigned long>(r.dropped));
  rec.set("camera", cam);

  JsonValue roi = JsonValue::makeObject();
  roi.set("x", r.roi.x);
  roi.set("y", r.roi.y);
  roi.set("w", r.roi.w);
  roi.set("h", r.roi.h);
  rec.set("roi", roi);

  JsonValue calib = JsonValue::makeObject();
  calib.set("id", r.calibrationId.empty() ? JsonValue() : JsonValue(r.calibrationId));
  calib.set("axis", std::string(1, r.axis));
  calib.set("mm_per_pixel", cal ? JsonValue(r.mmPerPx) : JsonValue());
  calib.set("working_distance_mm", m.distanceValid ? JsonValue(m.distanceMm) : JsonValue());
  rec.set("calibration", calib);

  JsonValue res = JsonValue::makeObject();
  res.set("valid", r.valid);
  res.set("reason", r.valid ? JsonValue() : JsonValue(r.reason));
  res.set("unit", m.unit);
  // A multi-point result is valid by its windows while its representative
  // waveform may not be: its amplitudes are then unknown, not zero.
  const bool wave = r.valid && (r.method != "multipoint" || r.multipoint.waveformValid);
  res.set("dominant_frequency_hz", numOrNull(r.dominantHz, r.valid));
  res.set("displacement_peak_to_peak_px", numOrNull(r.ppPx, wave));
  res.set("displacement_rms_px", numOrNull(r.rmsPx, wave));
  res.set("spectral_peak_px", numOrNull(r.peakAmpPx, wave));
  res.set("displacement_peak_to_peak_mm", numOrNull(r.ppPx * k, wave && cal));
  res.set("displacement_rms_mm", numOrNull(r.rmsPx * k, wave && cal));
  res.set("spectral_peak_mm", numOrNull(r.peakAmpPx * k, wave && cal));
  res.set("definitions", "peak_to_peak and rms of the linearly detrended displacement; spectral_peak is the 0-peak "
                         "amplitude at the dominant frequency (Hann window, coherent gain corrected, single-sided)");
  res.set("window", "hann");
  res.set("tracker", r.tracker.empty() ? JsonValue() : JsonValue(r.tracker));
  res.set("peak_snr", numOrNull(r.peakSnr, wave));
  res.set("peak_clear", r.valid && r.peakClear);
  JsonValue bg = JsonValue::makeObject();
  bg.set("used", r.reference.used);
  if (r.reference.used) {
    bg.set("valid", r.reference.valid);
    bg.set("reason", r.reference.valid ? JsonValue() : JsonValue(r.reference.reason));
    JsonValue br = JsonValue::makeObject();
    br.set("x", r.reference.roi.x);
    br.set("y", r.reference.roi.y);
    br.set("w", r.reference.roi.w);
    br.set("h", r.reference.roi.h);
    bg.set("roi", br);
    bg.set("peak_to_peak_px", numOrNull(r.reference.ppPx, r.reference.valid));
    bg.set("dominant_frequency_hz", numOrNull(r.reference.dominantHz, r.reference.valid));
    bg.set("raw_target_peak_to_peak_px", numOrNull(r.rawPpPx, r.rawPpPx > 0));
    bg.set("raw_target_dominant_frequency_hz", numOrNull(r.rawDominantHz, r.rawDominantHz > 0));
    bg.set("definition", "result = target minus background reference, translation only");
  }
  res.set("background_reference", bg);
  JsonValue fld = JsonValue::makeObject();
  fld.set("valid", r.field.valid);
  fld.set("reason", r.field.valid ? JsonValue() : JsonValue(r.field.reason));
  fld.set("peak_hz", numOrNull(r.field.peakHz, r.field.peakHz > 0));
  fld.set("fraction", r.field.fraction);
  fld.set("resolution_hz", r.field.resolutionHz);
  JsonValue lines = JsonValue::makeArray();
  for (const auto& l : r.field.lines) {
    JsonValue jl = JsonValue::makeObject();
    jl.set("hz", l.hz);
    jl.set("fraction", l.fraction);
    jl.set("harmonic_of_strongest", l.harmonicOf);
    lines.push(jl);
  }
  fld.set("lines", lines);
  fld.set("definition", "whole-field brightness spectral lines; frequency indicator only, no displacement unit");
  res.set("field_spectrum", fld);
  res.set("method", r.method);
  if (r.method == "multipoint") {
    const MeasureResult::MultiPoint& mp = r.multipoint;
    JsonValue j = JsonValue::makeObject();
    j.set("core", "AX7010_Vibration_Linux_20261021 (third_party/axvibration)");
    char hash[16];
    snprintf(hash, sizeof(hash), "%08x", mp.layoutHash);
    j.set("layout_hash", hash);
    j.set("baseline_file", mp.baselineFile.empty() ? JsonValue() : JsonValue(mp.baselineFile));
    j.set("unit", "projected_pixel_rms");
    const bool sig = mp.signalWindows > 0;
    j.set("frequency_hz", numOrNull(mp.frequencyHz, sig));
    j.set("frequency_min_hz", numOrNull(mp.frequencyMinHz, sig));
    j.set("frequency_max_hz", numOrNull(mp.frequencyMaxHz, sig));
    j.set("sample_rate_hz", numOrNull(mp.fs, mp.fs > 0));
    j.set("windows", mp.windows);
    j.set("signal_windows", mp.signalWindows);
    j.set("below_baseline_windows", mp.quietWindows);
    j.set("rejected_windows", mp.rejectedWindows);
    j.set("baseline_mismatch_windows", mp.mismatchWindows);
    j.set("passed_points", mp.passed);
    JsonValue pts = JsonValue::makeArray();
    for (size_t i = 0; i < mp.points.size(); ++i) {
      JsonValue p = JsonValue::makeObject();
      p.set("x", static_cast<double>(mp.points[i][0]));
      p.set("y", static_cast<double>(mp.points[i][1]));
      const bool gate = i < mp.gate.size() && mp.gate[i];
      p.set("valid", i < mp.valid.size() && mp.valid[i]);
      p.set("above_baseline", gate);
      p.set("gate_ratio", numOrNull(i < mp.gateRatio.size() ? mp.gateRatio[i] : 0, i < mp.gateRatio.size()));
      p.set("rms_px", numOrNull(gate ? mp.rmsPx[i] : 0, gate));
      pts.push(p);
    }
    j.set("points", pts);
    JsonValue refs = JsonValue::makeArray();
    for (const auto& q : mp.refs) {
      JsonValue p = JsonValue::makeObject();
      p.set("x", static_cast<double>(q[0]));
      p.set("y", static_cast<double>(q[1]));
      refs.push(p);
    }
    j.set("references", refs);
    j.set("waveform_point", mp.representative);
    j.set("waveform_valid", mp.waveformValid);
    j.set("waveform_reason", mp.waveformValid ? JsonValue() : JsonValue(mp.waveformReason));
    j.set("definition",
          "team multi-point core: 25x25 templates, background translation removed, 128-frame windows gated "
          "against a motor-off baseline (3x); rms_px is the 2D image-plane harmonic RMS, not mm; the waveform and "
          "spectrum files and the px metrics above describe waveform_point (null when its waveform failed the "
          "checks: waveform_reason)");
    res.set("multipoint", j);
  }
  res.set("level", levelKey(m.level));
  JsonValue th = JsonValue::makeObject();
  th.set("unit", "mm");
  th.set("binary_threshold", m.binaryThreshold);
  th.set("warn_peak_to_peak", m.warnPp);
  th.set("alarm_peak_to_peak", m.alarmPp);
  res.set("thresholds", th);
  res.set("waveform_file", "waveform.csv");
  res.set("spectrum_file", "spectrum.csv");
  rec.set("result", res);

  JsonValue q = JsonValue::makeObject();
  q.set("bad_frames", static_cast<unsigned long>(r.badFrames));
  q.set("search_edge_hits", static_cast<unsigned long>(r.edgeHits));
  q.set("unconverged_frames", static_cast<unsigned long>(r.unconvergedFrames));
  q.set("saturated_frames", static_cast<unsigned long>(r.saturatedFrames));
  q.set("min_ncc", numOrNull(r.minScore, r.frames > 0));
  rec.set("quality", q);

  JsonValue light = JsonValue::makeObject();
  light.set("on", m.lightOn);
  light.set("percent", m.lightPercent);
  rec.set("lighting", light);
  JsonValue laser = JsonValue::makeObject();
  laser.set("on", m.laserOn);
  laser.set("switched_off_for_capture", m.laserOffForCapture);
  rec.set("laser_projector", laser);

  JsonValue ref = JsonValue::makeObject();
  ref.set("model", "ADXL345");
  ref.set("available", !adxl.empty());
  ref.set("valid", false);  // register polling is not yet a qualified reference measurement
  ref.set("timing_validated", false);
  ref.set("reason", "sampling_timing_unqualified");
  ref.set("samples", static_cast<unsigned long>(adxl.size()));
  ref.set("timestamp_source", adxl.empty() ? JsonValue() : JsonValue("PL DRDY events or host polling; sample association unqualified"));
  ref.set("file", adxl.empty() ? JsonValue() : JsonValue("reference_adxl345.csv"));
  rec.set("reference", ref);
  JsonValue im = JsonValue::makeObject();
  im.set("model", "LSM6DSV");
  im.set("timing_validated", false);
  im.set("samples", static_cast<unsigned long>(imu.size()));
  im.set("file", imu.empty() ? JsonValue() : JsonValue("imu.csv"));
  rec.set("camera_imu", im);

  if (m.acoustic.enabled && !saveAcoustic(path, m.acoustic, rec)) {
    err = "write acoustic files";
    return false;
  }

  if (m.stereo.enabled) {
    JsonValue st = JsonValue::makeObject();
    st.set("backend", m.stereo.backend.empty() ? JsonValue() : JsonValue(m.stereo.backend));
    st.set("calibration_id", m.stereo.calibrationId.empty() ? JsonValue() : JsonValue(m.stereo.calibrationId));
    st.set("roi_depth_mm", numOrNull(m.stereo.roiDepthMm, m.stereo.roiDepthValid));
    st.set("used_for_scale", m.stereo.usedForScale);
    st.set("definition", "median measured stereo depth inside the ROI at capture start (hole-filled pixels excluded)");
    rec.set("stereo", st);
  }

  JsonValue env = JsonValue::makeObject();
  env.set("temperature_c", numOrNull(m.tempC, m.envValid));
  env.set("humidity_percent", numOrNull(m.rhPercent, m.envValid));
  env.set("distance_mm", m.distanceValid ? JsonValue(m.distanceMm) : JsonValue());
  env.set("valid", m.envValid);
  rec.set("environment", env);

  JsonValue ver = JsonValue::makeObject();
  ver.set("app", m.appVersion);
  ver.set("pl", m.plVersion);
  ver.set("nano", m.nanoVersion.empty() ? JsonValue() : JsonValue(m.nanoVersion));
  ver.set("hardware", "2026-10-20");
  rec.set("versions", ver);

  if (!writeFileAtomic(path + "/record.json", jsonPretty(rec))) {
    err = "write record.json";
    return false;
  }
  return true;
}

namespace {
bool readCsv(const std::string& file, int col, std::vector<double>& a, std::vector<double>& b) {
  std::ifstream f(file);
  if (!f) return false;
  std::string line;
  std::getline(f, line);  // header
  while (std::getline(f, line)) {
    std::vector<double> cells;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) cells.push_back(atof(cell.c_str()));
    if (static_cast<int>(cells.size()) <= col) continue;
    a.push_back(cells[0]);
    b.push_back(cells[col]);
  }
  return true;
}
}  // namespace

bool loadRecord(const std::string& dir, const std::string& id, JsonValue& record, std::vector<double>& t,
                std::vector<double>& disp, std::vector<double>& f, std::vector<double>& amp) {
  const std::string path = dir + "/" + id;
  std::string err;
  if (!loadJsonFile(path + "/record.json", record, &err)) return false;
  const bool mm = jStr(jObj(record, "result"), "unit", "px") == "mm";
  readCsv(path + "/waveform.csv", mm ? 3 : 1, t, disp);
  readCsv(path + "/spectrum.csv", mm ? 2 : 1, f, amp);
  return true;
}

void loadAcousticSpectrum(const std::string& dir, const std::string& id, std::vector<double>& f,
                          std::vector<double>& amp) {
  f.clear();
  amp.clear();
  readCsv(dir + "/" + id + "/acoustic_spectrum.csv", 1, f, amp);
}

std::string latestRecordId(const std::string& dir) {
  DIR* d = opendir(dir.c_str());
  if (!d) return std::string();
  std::string best;
  time_t bestTime = 0;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    struct stat st;
    const std::string rec = dir + "/" + e->d_name + "/record.json";
    if (stat(rec.c_str(), &st) == 0 && (best.empty() || st.st_mtime >= bestTime)) {
      best = e->d_name;
      bestTime = st.st_mtime;
    }
  }
  closedir(d);
  return best;
}

std::vector<RecordSummary> listRecords(const std::string& dir, size_t maxCount) {
  std::vector<std::pair<time_t, std::string>> found;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      if (e->d_name[0] == '.') continue;
      struct stat st;
      const std::string rec = dir + "/" + e->d_name + "/record.json";
      if (stat(rec.c_str(), &st) == 0) found.emplace_back(st.st_mtime, e->d_name);
    }
    closedir(d);
  }
  std::sort(found.begin(), found.end(), [](const std::pair<time_t, std::string>& a, const std::pair<time_t, std::string>& b) {
    return a.first != b.first ? a.first > b.first : a.second > b.second;
  });
  if (found.size() > maxCount) found.resize(maxCount);
  std::vector<RecordSummary> out;
  for (const auto& f : found) {
    JsonValue rec;
    if (!loadJsonFile(dir + "/" + f.second + "/record.json", rec, nullptr)) continue;
    const JsonValue& res = jObj(rec, "result");
    RecordSummary r;
    r.id = f.second;
    r.startLocal = jStr(rec, "start_local", "");
    r.pointId = jStr(rec, "point_id", "");
    r.pointName = jStr(rec, "point_name", "");
    r.valid = jBool(res, "valid", false);
    r.reason = jStr(res, "reason", "");
    r.unit = jStr(res, "unit", "px");
    const bool mm = r.unit == "mm";
    r.dominantHz = jNum(res, "dominant_frequency_hz", 0);
    r.peakClear = jBool(res, "peak_clear", true);
    r.amplitudes = jBool(jObj(res, "multipoint"), "waveform_valid", true);
    r.peakToPeak = jNum(res, mm ? "displacement_peak_to_peak_mm" : "displacement_peak_to_peak_px", 0);
    r.rms = jNum(res, mm ? "displacement_rms_mm" : "displacement_rms_px", 0);
    const std::string lv = jStr(res, "level", "unknown");
    r.level = lv == "alarm" ? AlarmLevel::kAlarm
                            : (lv == "warn" ? AlarmLevel::kWarn : (lv == "normal" ? AlarmLevel::kNormal : AlarmLevel::kUnknown));
    out.push_back(r);
  }
  return out;
}

}  // namespace vm
