#include "app/config.h"

#include <algorithm>
#include <cctype>

#include "util/json_util.h"

namespace vm {

namespace {

std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

int i32(const JsonValue& o, const char* k, int def) { return static_cast<int>(jInt(o, k, def)); }

// [lo, hi] in Hz.
bool band(const JsonValue& v, std::array<double, 2>& out) {
  if (!v.isArray() || v.size() != 2) return false;
  const double lo = v.at(0).asDouble(-1), hi = v.at(1).asDouble(-1);
  if (lo <= 0 || hi <= lo) return false;
  out = {lo, hi};
  return true;
}

}  // namespace

bool loadConfig(const std::string& path, AppConfig& c, std::string& err) {
  JsonValue root;
  if (!loadJsonFile(path, root, &err)) return false;
  if (!root.isObject()) {
    err = path + ": top level must be an object";
    return false;
  }
  c.deviceId = jStr(root, "device_id", c.deviceId);
  c.tzOffsetMin = i32(root, "timezone_offset_min", c.tzOffsetMin);

  const JsonValue& hw = jObj(root, "hardware");
  c.lampPwmEnabled = jBool(hw, "lamp_pwm_enabled", c.lampPwmEnabled);
  c.lampPwmBase = jAddr(hw, "lamp_pwm_base", c.lampPwmBase);
  c.carrierBase = jAddr(hw, "carrier_ctrl_base", c.carrierBase);
  c.i2cMgmtBase = jAddr(hw, "i2c_management_base", c.i2cMgmtBase);
  c.i2cSensBase = jAddr(hw, "i2c_sensors_base", c.i2cSensBase);
  c.uartNanoBase = jAddr(hw, "uart_nano_base", c.uartNanoBase);
  c.uartPrnBase = jAddr(hw, "uart_printer_base", c.uartPrnBase);
  c.spiSensBase = jAddr(hw, "spi_sensors_base", c.spiSensBase);
  const JsonValue& dev = jObj(hw, "devices");
  c.carrierDev = jStr(dev, "carrier_uio", c.carrierDev);
  c.i2cMgmtDev = jStr(dev, "i2c_management", c.i2cMgmtDev);
  c.i2cSensDev = jStr(dev, "i2c_sensors", c.i2cSensDev);
  c.uartNanoDev = jStr(dev, "uart_nano", c.uartNanoDev);
  c.uartPrnDev = jStr(dev, "uart_printer", c.uartPrnDev);
  c.spiImuDev = jStr(dev, "spi_imu", c.spiImuDev);
  c.spiAdxlDev = jStr(dev, "spi_adxl345", c.spiAdxlDev);

  const JsonValue& n = jObj(root, "nano");
  c.nano.baud = static_cast<uint32_t>(jInt(n, "baud", c.nano.baud));
  c.nano.flowControl = jBool(n, "flow_control", c.nano.flowControl);
  c.nano.timeoutMs = i32(n, "request_timeout_ms", c.nano.timeoutMs);
  c.nano.statusPeriodMs = i32(n, "status_period_ms", c.nano.statusPeriodMs);
  c.nano.offlineResetMs = i32(n, "offline_reset_ms", c.nano.offlineResetMs);
  c.nano.resetPulseMs = i32(n, "reset_pulse_ms", c.nano.resetPulseMs);

  const JsonValue& p = jObj(root, "panel");
  c.panel.brightness = i32(p, "brightness", c.panel.brightness);
  c.panel.countsPerDetent = i32(p, "encoder_counts_per_detent", c.panel.countsPerDetent);
  c.panel.reverse = jBool(p, "encoder_reverse", c.panel.reverse);
  c.panel.debounceMs = i32(p, "debounce_ms", c.panel.debounceMs);
  c.panel.longPressMs = i32(p, "long_press_ms", c.panel.longPressMs);
  c.panel.encFilterUs = i32(p, "encoder_filter_us", c.panel.encFilterUs);
  c.panel.watchdogMs = i32(p, "watchdog_ms", c.panel.watchdogMs);

  const JsonValue& l = jObj(root, "light");
  c.light.pwmHz = static_cast<uint32_t>(jInt(l, "pwm_hz", c.light.pwmHz));
  c.light.percent = jNum(l, "percent", c.light.percent);
  c.light.autoOnMeasure = jBool(l, "auto_on_measure", c.light.autoOnMeasure);
  c.laser.offDuringMeasure = jBool(jObj(root, "laser"), "off_during_measurement", c.laser.offDuringMeasure);

  const JsonValue& pw = jObj(root, "power");
  c.power.warnA = jNum(pw, "warn_current_a", c.power.warnA);
  c.power.denyA = jNum(pw, "deny_current_a", c.power.denyA);
  c.power.alertA = jNum(pw, "alert_current_a", c.power.alertA);

  const JsonValue& s = jObj(root, "sensors");
  const JsonValue& imu = jObj(s, "imu");
  c.imu.enabled = jBool(imu, "enabled", c.imu.enabled);
  c.imu.spiHz = static_cast<uint32_t>(jInt(imu, "spi_hz", c.imu.spiHz));
  c.imu.odrHz = i32(imu, "odr_hz", c.imu.odrHz);
  c.imu.accelG = i32(imu, "accel_range_g", c.imu.accelG);
  c.imu.gyroDps = i32(imu, "gyro_range_dps", c.imu.gyroDps);
  const JsonValue& ad = jObj(s, "adxl345");
  c.adxl.enabled = jBool(ad, "enabled", c.adxl.enabled);
  c.adxl.spiHz = static_cast<uint32_t>(jInt(ad, "spi_hz", c.adxl.spiHz));
  c.adxl.rateCode = i32(ad, "rate_code", c.adxl.rateCode);
  c.adxl.rangeG = i32(ad, "range_g", c.adxl.rangeG);
  const JsonValue& tof = jObj(s, "tof");
  c.tof.enabled = jBool(tof, "enabled", c.tof.enabled);
  c.tof.timingBudgetMs = i32(tof, "timing_budget_ms", c.tof.timingBudgetMs);
  c.tof.periodMs = i32(tof, "period_ms", c.tof.periodMs);
  c.tof.longRange = jStr(tof, "distance_mode", "long") == "long";
  const JsonValue& sht = jObj(s, "sht40");
  c.sht.enabled = jBool(sht, "enabled", c.sht.enabled);
  c.sht.periodMs = i32(sht, "period_ms", c.sht.periodMs);

  const JsonValue& pr = jObj(root, "printer");
  c.printer.enabled = jBool(pr, "enabled", c.printer.enabled);
  c.printer.baud = static_cast<uint32_t>(jInt(pr, "baud", c.printer.baud));
  c.printer.ctsFlow = jBool(pr, "cts_flow_control", c.printer.ctsFlow);
  c.printer.powerOnWaitMs = i32(pr, "power_on_wait_ms", c.printer.powerOnWaitMs);
  c.printer.readyTimeoutMs = i32(pr, "ready_timeout_ms", c.printer.readyTimeoutMs);
  c.printer.busyTimeoutMs = i32(pr, "cts_busy_timeout_ms", c.printer.busyTimeoutMs);
  c.printer.minOffMs = i32(pr, "min_off_ms", c.printer.minOffMs);
  c.printer.statusQuery = jStr(pr, "status_query", c.printer.statusQuery);
  c.printer.msPerRow = i32(pr, "ms_per_raster_row", c.printer.msPerRow);
  c.printer.finishMarginMs = i32(pr, "finish_margin_ms", c.printer.finishMarginMs);
  c.printer.stripRows = i32(pr, "raster_strip_rows", c.printer.stripRows);
  c.printer.rasterPauseMs = std::max(0, std::min(2000, i32(pr, "raster_pause_ms", c.printer.rasterPauseMs)));
  c.printer.widthDots = i32(pr, "width_dots", c.printer.widthDots);
  c.printer.feedLines = i32(pr, "feed_lines", c.printer.feedLines);
  c.printer.feedDots = i32(pr, "feed_dots", c.printer.feedDots);
  if (c.printer.feedDots < 0) c.printer.feedDots = 0;
  if (c.printer.feedDots > 4096) c.printer.feedDots = 4096;

  const JsonValue& cam = jObj(root, "camera");
  c.camera.source = jStr(cam, "source", c.camera.source);
  c.camera.v4l2.device = jStr(cam, "device", c.camera.v4l2.device);
  c.camera.v4l2.width = i32(cam, "width", c.camera.v4l2.width);
  c.camera.v4l2.height = i32(cam, "height", c.camera.v4l2.height);
  c.camera.v4l2.pixelFormat = jStr(cam, "pixel_format", c.camera.v4l2.pixelFormat);
  c.camera.v4l2.fps = i32(cam, "fps", c.camera.v4l2.fps);
  const JsonValue& pc = jObj(cam, "pl_vdma");
  c.camera.pl.bufferBase = jAddr(pc, "buffer_base", c.camera.pl.bufferBase);
  c.camera.pl.frameStride = jAddr(pc, "frame_stride", c.camera.pl.frameStride);
  c.camera.pl.fps = i32(pc, "fps", c.camera.pl.fps) == 30 ? 30 : 45;
  c.camera.pl.leftCamera = i32(pc, "left_camera", c.camera.pl.leftCamera) == 0 ? 0 : 1;
  const JsonValue& syn = jObj(cam, "synthetic");
  c.camera.synthetic.freqHz = jNum(syn, "freq_hz", c.camera.synthetic.freqHz);
  c.camera.synthetic.amplitudePx = jNum(syn, "amplitude_px", c.camera.synthetic.amplitudePx);
  c.camera.synthetic.fps = jNum(syn, "fps", c.camera.synthetic.fps);

  const JsonValue& m = jObj(root, "measurement");
  c.measure.roiW = i32(m, "roi_width_px", c.measure.roiW);
  c.measure.roiH = i32(m, "roi_height_px", c.measure.roiH);
  c.measure.searchRadius = i32(m, "search_radius_px", c.measure.searchRadius);
  c.measure.durationS = jNum(m, "duration_s", c.measure.durationS);
  const std::string axis = jStr(m, "axis", std::string(1, c.measure.axis));
  c.measure.axis = axis == "x" ? 'x' : 'y';
  c.measure.minScore = jNum(m, "min_ncc", c.measure.minScore);
  c.measure.fMinHz = jNum(m, "min_freq_hz", c.measure.fMinHz);
  const std::string tracker = jStr(m, "tracker", c.measure.tracker);
  c.measure.tracker = tracker == "lk" ? "lk" : "ncc";
  const std::string method = jStr(m, "method", c.measure.method);
  c.measure.method = method == "multipoint" ? "multipoint" : "roi";
  const JsonValue& mpj = jObj(m, "multipoint");
  MultiPointConfig& mp = c.measure.multipoint;
  mp.maxPoints = std::max(1, std::min(60, i32(mpj, "max_points", mp.maxPoints)));
  mp.gridCols = std::max(0, std::min(60, i32(mpj, "grid_cols", mp.gridCols)));
  mp.gridRows = std::max(0, std::min(60, i32(mpj, "grid_rows", mp.gridRows)));
  mp.pointPitchPx = std::max(25, i32(mpj, "point_pitch_px", mp.pointPitchPx));
  mp.refPitchPx = std::max(25, i32(mpj, "ref_pitch_px", mp.refPitchPx));
  mp.baselineS = std::max(5.0, jNum(mpj, "baseline_s", mp.baselineS));
  mp.baselineDir = jStr(mpj, "baseline_dir", mp.baselineDir);
  // A box saved for another method or tracker is cut to what this one takes.
  c.measure.roiW = std::max(kRoiMinPx, std::min(c.measure.roiW, roiMaxPx(c.measure)));
  c.measure.roiH = std::max(kRoiMinPx, std::min(c.measure.roiH, roiMaxPx(c.measure)));
  c.measure.minPeakSnr = jNum(m, "min_peak_snr", c.measure.minPeakSnr);
  c.measure.maxFinalStepPx = jNum(m, "lk_max_final_step_px", c.measure.maxFinalStepPx);
  c.measure.fieldSpectrum = jBool(m, "field_spectrum", c.measure.fieldSpectrum);
  const JsonValue& ref = jObj(m, "reference_roi");
  c.measure.referenceEnabled = jBool(ref, "enabled", c.measure.referenceEnabled);
  c.measure.refCx = jNum(ref, "x", c.measure.refCx);
  c.measure.refCy = jNum(ref, "y", c.measure.refCy);
  c.roiCenterX = jNum(m, "roi_center_x", c.roiCenterX);
  c.roiCenterY = jNum(m, "roi_center_y", c.roiCenterY);
  c.calibrationFile = jStr(m, "calibration_file", c.calibrationFile);

  c.stereo.scaleSource = jStr(m, "scale_source", c.stereo.scaleSource);
  if (c.stereo.scaleSource != "stereo" && c.stereo.scaleSource != "file_or_stereo") c.stereo.scaleSource = "file";

  const JsonValue& st = jObj(root, "stereo");
  c.stereo.enabled = jBool(st, "enabled", c.stereo.enabled);
  c.stereo.right = c.camera.v4l2;
  c.stereo.right.device = jStr(st, "right_device", "/dev/video1");
  c.stereo.service.calibrationFile = jStr(st, "calibration_file", c.stereo.service.calibrationFile);
  c.stereo.service.workWidth = i32(st, "work_width", c.stereo.service.workWidth);
  c.stereo.service.workHeight = i32(st, "work_height", c.stereo.service.workHeight);
  c.stereo.service.rateHz = jNum(st, "rate_hz", c.stereo.service.rateHz);
  c.stereo.service.maxSkewMs = jNum(st, "max_skew_ms", c.stereo.service.maxSkewMs);
  c.stereo.service.sgm.maxDisp = i32(st, "max_disparity", c.stereo.service.sgm.maxDisp);
  c.stereo.service.sgm.census = i32(st, "census", c.stereo.service.sgm.census);
  c.stereo.service.sgm.paths = i32(st, "paths", c.stereo.service.sgm.paths);
  c.stereo.service.sgm.refineLk = jBool(st, "refine_lk", c.stereo.service.sgm.refineLk);
  c.stereo.service.sgm.refineReject = i32(st, "refine_reject", c.stereo.service.sgm.refineReject);
  c.stereo.service.sgm.fillHoles = jBool(st, "fill_holes", c.stereo.service.sgm.fillHoles);
  c.stereo.service.sgm.speckleSize = i32(st, "speckle_px", c.stereo.service.sgm.speckleSize);
  c.stereo.service.temporal = jBool(st, "temporal_filter", c.stereo.service.temporal);
  c.stereo.service.backend = jStr(st, "backend", c.stereo.service.backend) == "ps" ? "ps" : "pl";
  c.stereo.service.plCoreBase = jAddr(st, "pl_core_base", c.stereo.service.plCoreBase);
  c.stereo.service.plDmaBase = jAddr(st, "pl_dma_base", c.stereo.service.plDmaBase);
  c.stereo.service.plBufferBase = jAddr(st, "pl_buffer_base", c.stereo.service.plBufferBase);
  c.stereo.service.plBufferSize = static_cast<uint32_t>(jAddr(st, "pl_buffer_size", c.stereo.service.plBufferSize));
  c.stereo.service.plVerifyEvery = i32(st, "pl_verify_every", c.stereo.service.plVerifyEvery);
  c.stereo.service.plSearchRanges = std::max(1, std::min(2, i32(st, "pl_search_ranges", c.stereo.service.plSearchRanges)));
  c.stereo.service.plVerticalCheck = jBool(st, "pl_vertical_check", c.stereo.service.plVerticalCheck);

  const JsonValue& ac = jObj(root, "acoustic");
  c.acoustic.enabled = jBool(ac, "enabled", c.acoustic.enabled);
  c.acoustic.saveWav = jBool(ac, "save_wav", c.acoustic.saveWav);
  AcousticOptions& ao = c.acoustic.service;
  ao.source = jStr(ac, "source", ao.source) == "synthetic" ? "synthetic" : "pl";
  ao.base = jAddr(ac, "base", ao.base);
  ao.phase = jStr(ac, "phase", ao.phase);
  if (ao.phase != "low" && ao.phase != "high") ao.phase = "auto";
  ao.warmupMs = i32(ac, "warmup_ms", ao.warmupMs);
  ao.ringS = jNum(ac, "ring_s", ao.ringS);
  ao.pollMs = std::max(2, i32(ac, "poll_ms", ao.pollMs));
  ao.synthHz = jNum(ac, "synthetic_hz", ao.synthHz);
  std::array<double, 2> b;
  if (const JsonValue* v = ac.find("analysis_band_hz"); v && band(*v, b)) {
    ao.analysis.bandLowHz = b[0];
    ao.analysis.bandHighHz = b[1];
  }
  if (const JsonValue* v = ac.find("carrier_bands_hz"); v && v->isArray() && v->size() > 0) {
    std::vector<std::array<double, 2>> bands;
    for (const JsonValue& e : v->items())
      if (band(e, b)) bands.push_back(b);
    if (!bands.empty()) ao.analysis.carrierBands = bands;
  }
  ao.analysis.minProminenceDb = jNum(ac, "min_prominence_db", ao.analysis.minProminenceDb);

  const JsonValue& a = jObj(root, "alarm");
  c.alarm.warnPp = jNum(a, "warn_peak_to_peak", c.alarm.warnPp);
  c.alarm.alarmPp = jNum(a, "alarm_peak_to_peak", c.alarm.alarmPp);
  c.alarm.buzzer = jBool(a, "buzzer", c.alarm.buzzer);
  c.alarm.binaryThreshold = jBool(a, "binary_threshold", c.alarm.binaryThreshold);

  const JsonValue& v = jObj(root, "voice");
  c.voice.enabled = jBool(v, "enabled", c.voice.enabled);
  c.voice.volume = i32(v, "volume", c.voice.volume);

  const JsonValue& f = jObj(root, "fan");
  c.fan.mode = jStr(f, "mode", c.fan.mode);
  c.fan.onTempC = jNum(f, "on_temp_c", c.fan.onTempC);
  c.fan.offTempC = jNum(f, "off_temp_c", c.fan.offTempC);
  c.fan.pauseDuringMeasure = jBool(f, "pause_during_measurement", c.fan.pauseDuringMeasure);

  const JsonValue& vis = jObj(root, "vision");
  c.vision.enabled = jBool(vis, "enabled", c.vision.enabled);
  c.vision.levels = i32(vis, "levels", c.vision.levels);  // 0 = automatic
  c.vision.lowHz = jNum(vis, "low_hz", c.vision.lowHz);
  c.vision.highHz = jNum(vis, "high_hz", c.vision.highHz);
  c.vision.gain = i32(vis, "gain", c.vision.gain);

  c.recordsDir = jStr(jObj(root, "storage"), "records_dir", c.recordsDir);
  c.settingsFile = jStr(jObj(root, "storage"), "settings_file", c.settingsFile);

  const JsonValue& ui = jObj(root, "ui");
  c.ui.enabled = jBool(ui, "enabled", c.ui.enabled);
  c.ui.display = jStr(ui, "display", c.ui.display);
  c.ui.fbdev = jStr(ui, "fbdev", c.ui.fbdev);
  c.ui.pl.bufferBase = static_cast<uint32_t>(jAddr(ui, "pl_framebuffer", c.ui.pl.bufferBase));
  c.ui.touchDev = jStr(ui, "touch_device", c.ui.touchDev);
  c.ui.swapXY = jBool(ui, "swap_xy", c.ui.swapXY);
  c.ui.invertX = jBool(ui, "invert_x", c.ui.invertX);
  c.ui.invertY = jBool(ui, "invert_y", c.ui.invertY);

  c.controlSocket = jStr(root, "control_socket", c.controlSocket);

  const JsonValue& api = jObj(root, "app_api");
  c.appApi.enabled = jBool(api, "enabled", c.appApi.enabled);
  c.appApi.httpPort = std::max(0, std::min(65535, i32(api, "http_port", c.appApi.httpPort)));
  c.appApi.discoveryPort = std::max(-1, std::min(65535, i32(api, "discovery_port", c.appApi.discoveryPort)));
  c.appApi.name = jStr(api, "name", c.appApi.name);
  c.appApi.requirePairing = jBool(api, "require_pairing", c.appApi.requirePairing);
  c.appApi.tokensFile = jStr(api, "tokens_file", c.appApi.tokensFile);
  c.appApi.pinTimeoutS = std::max(30, std::min(600, i32(api, "pin_timeout_s", c.appApi.pinTimeoutS)));
  const JsonValue& gw = jObj(root, "app_gateway");
  c.gateway.enabled = jBool(gw, "enabled", c.gateway.enabled);
  c.gateway.statusPeriodMs = std::max(500, i32(gw, "status_period_ms", c.gateway.statusPeriodMs));
  c.poweroffOnShutdown = jBool(root, "poweroff_on_shutdown", c.poweroffOnShutdown);

  const JsonValue& pts = jObj(root, "points");
  for (const auto& mm : pts.members()) {
    PointInfo pi;
    pi.id = jStr(mm.value, "id", mm.key);
    pi.name = jStr(mm.value, "name", "");
    c.points[upper(mm.key)] = pi;
  }
  return true;
}

bool loadCalibration(const std::string& path, MeasureConfig& m, std::string& err) {
  JsonValue root;
  if (!loadJsonFile(path, root, &err)) return false;
  const double mmPerPx = jNum(root, "mm_per_px", 0);
  if (mmPerPx <= 0) {
    err = path + ": mm_per_px missing or not positive";
    return false;
  }
  m.mmPerPx = mmPerPx;
  m.calibrationId = jStr(root, "id", "");
  m.calibrationLayoutHash = jStr(root, "layout_hash", "");
  const std::string axis = jStr(root, "axis", "");
  if (axis == "x" || axis == "y") m.axis = axis[0];
  return true;
}

}  // namespace vm
