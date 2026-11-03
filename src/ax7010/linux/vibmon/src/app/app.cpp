#include "app/app.h"

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>

#include "app/report.h"
#include "app/storage.h"
#include "gfx/font.h"
#include "hw/sysfs_find.h"
#include "measure/pl_camera_source.h"
#include "ui/fb_ui.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "app";

JsonValue okReply(const JsonValue& data = JsonValue()) {
  JsonValue r = JsonValue::makeObject();
  r.set("ok", true);
  if (!data.isNull()) r.set("data", data);
  return r;
}

JsonValue errReply(const std::string& e) {
  JsonValue r = JsonValue::makeObject();
  r.set("ok", false);
  r.set("error", e);
  return r;
}

std::string upperHex(std::string s) {
  for (auto& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  return s;
}

bool dirExists(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

const char* measureStateName(MeasurementEngine::State s) {
  switch (s) {
    case MeasurementEngine::State::kStopped: return "stopped";
    case MeasurementEngine::State::kPreview: return "preview";
    case MeasurementEngine::State::kCapturing: return "capturing";
    case MeasurementEngine::State::kAnalyzing: return "analyzing";
  }
  return "stopped";
}
}  // namespace

Application::Application(const AppConfig& cfg) : cfg_(cfg) {}

Application::~Application() {
  if (lampPwm_.ready()) lampPwm_.setProjector(false);
  if (lampPwm_.ready()) lampPwm_.set(false, 0);
  ui_.reset();
  ctl_.reset();
  appApi_.reset();
  if (engine_) engine_->stopCamera();
  if (stereo_) stereo_->stop();
  if (acoustic_) acoustic_->stop();
  if (sensors_) sensors_->stop();
  printer_.reset();
}

// ---------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------

void Application::openDevices() {
  std::string dev = cfg_.carrierDev.empty() ? findUioByBase(cfg_.carrierBase) : cfg_.carrierDev;
  if (!dev.empty() && uio_.open(dev, creg::kMapSize)) {
    carrier_.reset(new CarrierCtrl(uio_));
    uint32_t ver = 0;
    carrierOk_ = carrier_->probe(&ver);
    if (carrierOk_) {
      LOGI(kTag, "carrier_ctrl %s v%u.%u, %u Hz", dev.c_str(), ver >> 16, ver & 0xFFFF, carrier_->clockHz());
      tickClock_.setHz(carrier_->clockHz());
      char buf[64];
      snprintf(buf, sizeof(buf), "carrier_ctrl v%u.%u · %.0f MHz", ver >> 16, ver & 0xFFFF, carrier_->clockHz() / 1e6);
      const std::string pl = buf;
      state_.update([&pl](SystemState& s) { s.plVersion = pl; });
    } else {
      LOGE(kTag, "%s does not identify as carrier_ctrl", dev.c_str());
    }
  } else {
    LOGE(kTag, "carrier_ctrl UIO device not found (AXI 0x%08llx)", static_cast<unsigned long long>(cfg_.carrierBase));
  }

  if (!carrierOk_ && cfg_.lampPwmEnabled) {
    if (lampPwm_.open(cfg_.lampPwmBase)) LOGI(kTag, "J301 lamp PWM / J304 projector backend ready (v2)");
    else LOGE(kTag, "J301 lamp PWM identification failed; output unavailable");
  }
  dev = cfg_.i2cMgmtDev.empty() ? findI2cDevByBase(cfg_.i2cMgmtBase) : cfg_.i2cMgmtDev;
  if (dev.empty() || !i2cMgmt_.open(dev)) LOGE(kTag, "management I2C (I2C0) not available");
  dev = cfg_.i2cSensDev.empty() ? findI2cDevByBase(cfg_.i2cSensBase) : cfg_.i2cSensDev;
  if (dev.empty() || !i2cSens_.open(dev)) LOGW(kTag, "sensor I2C (I2C1) not available");

  dev = cfg_.spiImuDev.empty() ? findSpidevByBase(cfg_.spiSensBase, 0) : cfg_.spiImuDev;
  if (cfg_.imu.enabled && (dev.empty() || !spiImu_.open(dev, 0, cfg_.imu.spiHz))) LOGW(kTag, "IMU spidev missing");
  dev = cfg_.spiAdxlDev.empty() ? findSpidevByBase(cfg_.spiSensBase, 1) : cfg_.spiAdxlDev;
  if (cfg_.adxl.enabled && (dev.empty() || !spiAdxl_.open(dev, 3, cfg_.adxl.spiHz))) LOGW(kTag, "ADXL345 spidev missing");

  dev = cfg_.uartNanoDev.empty() ? findTtyByBase(cfg_.uartNanoBase) : cfg_.uartNanoDev;
  if (dev.empty() || !nanoPort_.open(dev, cfg_.nano.baud, cfg_.nano.flowControl)) {
    LOGE(kTag, "Nano UART not available");
  }
}

bool Application::init() {
  // Worker threads inherit this mask; install it before creating any of them.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) return false;
  std::string err;
  if (!cfg_.calibrationFile.empty() && loadCalibration(cfg_.calibrationFile, cfg_.measure, err)) {
    LOGI(kTag, "calibration %s: %.6g mm/px (axis %c)", cfg_.measure.calibrationId.c_str(), cfg_.measure.mmPerPx,
         cfg_.measure.axis);
  } else {
    LOGW(kTag, "no valid calibration (%s): results stay in pixels", err.c_str());
  }
  state_.update([&](SystemState& s) {
    s.lightPercent = cfg_.light.percent;
    s.roiCx = cfg_.roiCenterX;
    s.roiCy = cfg_.roiCenterY;
    s.method = cfg_.measure.method;
    s.refCx = cfg_.measure.refCx;
    s.refCy = cfg_.measure.refCy;
    s.deviceId = cfg_.deviceId;
    s.appVersion = std::string("vibmon ") + VIBMON_APP_VERSION;
    s.calibrated = cfg_.measure.mmPerPx > 0;
    s.calibrationId = cfg_.measure.calibrationId;
    s.imuEnabled = cfg_.imu.enabled;
    s.adxlEnabled = cfg_.adxl.enabled;
    s.tofEnabled = cfg_.tof.enabled;
    s.shtEnabled = cfg_.sht.enabled;
    s.printerEnabled = cfg_.printer.enabled;
    s.powerWarnA = cfg_.power.warnA;
    s.powerDenyA = cfg_.power.denyA;
  });
  publishSettings();

  openDevices();
  state_.update([&](SystemState& s) { s.carrierOk = carrierOk_; });

  board_.reset(new BoardService(i2cMgmt_, cfg_, state_));
  if (i2cMgmt_.isOpen()) {
    board_->init();
    board_->allLoadsOff();
    loadRtcTime();
  }
  alarm_.reset(new AlarmService(*board_, cfg_.alarm));

  if (carrierOk_) {
    carrier_->setPwmEnable(false, false);
    carrier_->setPwmFrequency(cfg_.light.pwmHz);
    carrier_->setPwmKills(true, true);
    panel_.reset(new PanelService(*carrier_, cfg_.panel));
    PanelService::Callbacks cb;
    cb.keyShort = [this]() { muteAlarm(); };
    cb.keyLong = [this]() { requestShutdown(); };
    // With the touch UI the encoder acts on the shown page; without it, it zooms.
    cb.encoderSteps = [this](int steps) {
      if (ui_) {
        ui_->onEncoder(steps);
        return;
      }
      state_.update([steps](SystemState& s) {
        s.zoom = std::max(1.0, std::min(8.0, s.zoom * std::pow(1.25, steps)));
      });
    };
    cb.encoderPress = [this]() {
      if (ui_) {
        ui_->onEncoderPress(false);
        return;
      }
      state_.update([](SystemState& s) {
        s.zoom = 1.0;
        s.zoomCx = s.zoomCy = 0.5;
      });
    };
    cb.encoderLong = [this]() {
      if (ui_) ui_->onEncoderPress(true);
    };
    panel_->setCallbacks(cb);
    panel_->init();
    setupCarrierEvents();
    const uint32_t in = carrier_->inputs();
    if (((in >> kSrcPfailN) & 1u) == 0) powerFail(true);
  }

  // Nano link.
  NanoLink::Config nc;
  nc.timeoutMs = cfg_.nano.timeoutMs;
  nc.statusPeriodMs = cfg_.nano.statusPeriodMs;
  nc.offlineResetMs = board_->tcaReady() ? cfg_.nano.offlineResetMs : 0;
  nc.axVersion = std::string("vibmon ") + VIBMON_APP_VERSION;
  nanoChan_.reset(new SerialChannel(nanoPort_));
  nano_.reset(new NanoLink(*nanoChan_, nc));
  nano_->setEventHandler([this](const NanoEvent& e) { onNanoEvent(e); });
  nano_->setOnlineHandler([this](bool online, const std::string& boot, bool newBoot) {
    state_.update([online](SystemState& s) { s.nanoOnline = online; });
    if (online) {
      nanoOfflineSinceMs_ = 0;
      LOGI(kTag, "Nano online (boot %s%s)", boot.c_str(), newBoot ? ", new" : "");
      syncTimeFromNano();
    } else if (nanoOfflineSinceMs_ == 0) {
      nanoOfflineSinceMs_ = monoMs();
    }
  });
  nano_->setResetHook([this]() {
    if (shutdown_ || pfail_) return;
    board_->setNanoReset(true);
    loop_.addTimer(cfg_.nano.resetPulseMs, [this]() { board_->setNanoReset(false); }, false);
  });
  if (nanoPort_.isOpen()) {
    loop_.addFd(nanoPort_.fd(), EPOLLIN, [this](uint32_t) { nano_->onReadable(monoMs()); });
    if (carrierOk_) nano_->setIrqLevel(((carrier_->inputs() >> kSrcEspIrq) & 1u) != 0);
  }
  nanoOfflineSinceMs_ = monoMs();

  // Sensors (own thread).
  sensors_.reset(new SensorService(cfg_, state_, tickClock_));
  sensors_->start(i2cSens_.isOpen() ? &i2cSens_ : nullptr, spiImu_.isOpen() ? &spiImu_ : nullptr,
                  spiAdxl_.isOpen() ? &spiAdxl_ : nullptr);

  // Printer (own thread).
  const std::string prnDev = cfg_.uartPrnDev.empty() ? findTtyByBase(cfg_.uartPrnBase) : cfg_.uartPrnDev;
  if (prnDev.empty()) LOGW(kTag, "printer UART not found");
  if (cfg_.printer.enabled && prnDev.empty()) {
    state_.update([](SystemState& s) { s.printerError = "no_printer_uart"; });
  }
  printer_.reset(new PrinterService(cfg_.printer, *board_, state_, prnDev));
  printer_->setGate([this]() -> std::string {
    // No synchronous call into the event loop: it may already be exiting.
    if (shutdown_) return "shutdown";
    if (pfail_) return "power_fail";
    if (measuring_) return "measuring";
    if (!board_->heavyLoadAllowed()) return "power_budget";
    const SystemState st = state_.snapshot();
    if (st.lightOn || st.laserOn) return "lights_on";
    return std::string();
  });
  printer_->setDoneHandler([this](bool ok, const std::string& id, const std::string& error) {
    loop_.post([this, ok, id, error]() { onPrintDone(ok, id, error); });
  });

  // Camera.
  std::unique_ptr<FrameSource> src;
  std::unique_ptr<FrameSource> right;
  if (cfg_.camera.source == "synthetic") {
    src = makeSyntheticSource(cfg_.camera.synthetic);
  } else if (cfg_.camera.source == "pl_vdma") {
    auto sources = makePlCameraSources(cfg_.camera.pl);
    src = std::move(sources[0]);
    right = std::move(sources[1]);
  } else {
    src = makeV4l2Source(cfg_.camera.v4l2);
  }
  engine_.reset(new MeasurementEngine(std::move(src)));
  engine_->setVision(cfg_.vision);
  if (!engine_->startCamera()) {
    LOGW(kTag, "camera not available; retrying every 5 s");
    cameraRetryMs_ = monoMs() + 5000;
  }
  if (cfg_.stereo.enabled) {
    // PL stereo core (add_stereo.tcl); without it the bit-exact model runs on the PS.
    std::unique_ptr<PlStereo> pl;
    const StereoConfig& sc = cfg_.stereo.service;
    if (sc.backend == "pl") {
      std::string perr;
      std::unique_ptr<StereoPlIo> io =
          sc.plDmaBase ? openStereoPhys(sc.plCoreBase, sc.plDmaBase, sc.plBufferBase, sc.plBufferSize, perr)
                       : openStereoUio(sc.plCoreBase, perr);
      if (io) {
        pl.reset(new PlStereo(std::move(io)));
        if (!pl->probe(perr)) pl.reset();
      }
      if (pl) LOGI(kTag, "stereo: PL core found (max width %d)", pl->maxWidth());
      else LOGW(kTag, "stereo: PL core not available (%s); using the bit-exact model on the PS", perr.c_str());
    }
    if (!right) right = makeV4l2Source(cfg_.stereo.right);
    stereo_.reset(new StereoService(cfg_.stereo.service, std::move(right),
                                    [this](std::shared_ptr<const DepthMap> d) { publishDepth(std::move(d)); },
                                    std::move(pl)));
    if (stereo_->start()) {
      StereoService* svc = stereo_.get();
      engine_->setFrameTap([svc](GrayFrame& f) { svc->offerLeft(f); }, cfg_.stereo.service.rateHz);
      stereoStarted_ = true;
    } else {
      LOGW(kTag, "stereo: right camera %s not available", cfg_.stereo.right.device.c_str());
    }
  }

  // Acoustic reference (MP34DT01 on J706, PL pdm_mic).
  if (cfg_.acoustic.enabled) {
    acoustic_.reset(new AcousticService(cfg_.acoustic.service));
    acoustic_->start();
  }

  // Control socket and UI.
  ctl_.reset(new ControlServer(loop_, [this](const JsonValue& c) { return handleCommand(c); }));
  ctl_->listen(cfg_.controlSocket);
  startAppApi();
  if (cfg_.ui.enabled) {
    ui_.reset(new FbUi(*this));
    if (!ui_->start()) ui_.reset();
  }

  // Signals -> orderly exit.
  int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sfd >= 0) {
    loop_.addFd(sfd, EPOLLIN, [this, sfd](uint32_t) {
      signalfd_siginfo si;
      while (read(sfd, &si, sizeof(si)) > 0) {
      }
      requestExit();
    });
  }

  loop_.addTimer(20, [this]() {
    pumpCarrierEvents();
    nano_->tick(monoMs());
  });
  loop_.addTimer(100, [this]() { every100ms(); });
  loop_.addTimer(200, [this]() { every200ms(); });
  loop_.addTimer(1000, [this]() { every1s(); });

  state_.update([](SystemState& s) { s.initDone = true; });
  if (panel_) panel_->setSystem(PanelService::Sys::kOk);
  LOGI(kTag, "vibmon %s started", VIBMON_APP_VERSION);
  return true;
}

void Application::setupCarrierEvents() {
  const uint32_t rise = (1u << kSrcImuInt1) | (1u << kSrcAdxlDrdy) | (1u << kSrcRtc1Hz) | (1u << kSrcTach) |
                        (1u << kSrcPfailN) | (1u << kSrcEspIrq) | (1u << kSrcKey) | (1u << kSrcEncSw) |
                        (1u << kSrcKeyLong) | (1u << kSrcEncSwLong) | (1u << kSrcEncStep);
  const uint32_t fall = (1u << kSrcPfailN) | (1u << kSrcExpIntN) | (1u << kSrcEspIrq) | (1u << kSrcKey) |
                        (1u << kSrcEncSw);
  carrier_->enableIrq(false);
  carrier_->clearEvents();
  carrier_->configureEvents(rise, fall);
  carrier_->enableIrq(true);
  uio_.enableIrq();
  loop_.addFd(uio_.fd(), EPOLLIN, [this](uint32_t) {
    uio_.ackIrq();
    pumpCarrierEvents();
    uio_.enableIrq();
  });
}

void Application::pumpCarrierEvents() {
  if (!carrierOk_) return;
  std::vector<CarrierEvent> events;
  for (int round = 0; round < 8; ++round) {
    if (carrier_->popEvents(events, 256) == 0) break;
  }
  for (const auto& e : events) onCarrierEvent(e);
}

void Application::onCarrierEvent(const CarrierEvent& e) {
  switch (e.source) {
    case kSrcImuInt1:
    case kSrcAdxlDrdy:
      sensors_->onDrdy(e);
      break;
    case kSrcEspIrq:
      nano_->setIrqLevel(e.level);
      break;
    case kSrcExpIntN:
      if (!e.level && board_) board_->pollInputs();
      break;
    case kSrcPfailN:
      if (!e.level) {
        powerFail(true);
      } else {
        pfailHighSinceMs_ = monoMs();
      }
      break;
    case kSrcTach: {
      if (lastTachTicks_ != 0 && e.ticks > lastTachTicks_) {
        tachPeriods_.push_back(static_cast<double>(e.ticks - lastTachTicks_) / carrier_->clockHz());
        if (tachPeriods_.size() > 16) tachPeriods_.pop_front();
      }
      lastTachTicks_ = e.ticks;
      lastTachMs_ = monoMs();
      break;
    }
    case kSrcRtc1Hz:
      break;
    default:
      if (panel_) panel_->onEvent(e);
      break;
  }
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

void Application::loadRtcTime() {
  int64_t sec = 0;
  bool valid = false;
  if (!board_->readRtc(sec, valid)) return;
  if (!valid) {
    LOGW(kTag, "DS3231 time invalid (oscillator stopped / never set)");
    return;
  }
  const int64_t delta = sec * 1000 - realtimeMs();
  if (std::llabs(delta) > 2000 && !setRealtimeMs(sec * 1000)) {
    LOGW(kTag, "cannot set the system clock from RTC");
    return;
  }
  state_.update([](SystemState& s) {
    s.timeValid = true;
    s.timeSource = "rtc";
  });
}

void Application::applyTime(int64_t unixMs, const char* source) {
  const int64_t delta = unixMs - realtimeMs();
  const bool valid = state_.snapshot().timeValid;
  if (!valid || std::llabs(delta) > 500) {
    if (!setRealtimeMs(unixMs)) {
      LOGW(kTag, "setting system time failed");
      return;
    }
    LOGI(kTag, "system time set from %s (delta %lld ms)", source, static_cast<long long>(delta));
  }
  // Write the RTC on the next whole second (it has 1 s resolution).
  const int wait = static_cast<int>(1000 - realtimeMs() % 1000);
  loop_.addTimer(wait, [this]() { board_->writeRtc((realtimeMs() + 500) / 1000); }, false);
  const std::string src = source;
  state_.update([src](SystemState& s) {
    s.timeValid = true;
    s.timeSource = src;
  });
  lastTimeSyncMs_ = monoMs();
}

void Application::syncTimeFromNano() {
  if (!nano_->online()) return;
  const int64_t sent = monoMs();
  nano_->request(vp::kOpGetTime, JsonValue::makeObject(), [this, sent](const NanoReply& r) {
    bool valid = false;
    int64_t unixMs = 0;
    if (!r.ok || !r.data.getBool("valid", valid) || !valid || !r.data.getInt("unix_ms", unixMs)) return;
    const int64_t rtt = monoMs() - sent;
    applyTime(unixMs + rtt / 2, "ntp");
  });
}

// ---------------------------------------------------------------------------
// Nano events
// ---------------------------------------------------------------------------

void Application::onNanoEvent(const NanoEvent& e) {
  const int64_t now = monoMs();
  if (e.type == "NFC_TAG") {
    const std::string uid = upperHex(e.data.find("uid") ? e.data.find("uid")->asString() : std::string());
    auto it = cfg_.points.find(uid);
    const std::string id = it != cfg_.points.end() ? it->second.id : "UID-" + uid;
    const std::string name = it != cfg_.points.end() ? it->second.name : std::string();
    state_.update([&](SystemState& s) {
      s.pointUid = uid;
      s.pointId = id;
      s.pointName = name;
    });
    if (panel_) panel_->flash(PanelService::Flash::kNfc, now);
    setMessage("测点: " + id + (name.empty() ? "" : " " + name));
  } else if (e.type == "WIFI_STATE" || e.type == "SD_STATE") {
    updateFromNanoStatus();
  } else if (e.type == "TIME_SYNC") {
    syncTimeFromNano();
  } else if (e.type == "AUDIO_FAILED") {
    LOGW(kTag, "voice failed: %s", vp::jsonSerialize(e.data).c_str());
  } else if (e.type == "FAULT") {
    LOGW(kTag, "Nano fault: %s", vp::jsonSerialize(e.data).c_str());
  } else if (e.type == "APP_COMMAND" || e.type == "APP_CLIENT") {
    onAppEvent(e);
  } else if (e.type == "BOOT") {
    LOGI(kTag, "Nano booted: %s", vp::jsonSerialize(e.data).c_str());
  }
}

void Application::updateFromNanoStatus() {
  const JsonValue& st = nano_->lastStatus();
  const JsonValue& wifi = jObj(st, "wifi");
  const std::string ws = jStr(wifi, "state", "UNKNOWN");
  state_.update([&](SystemState& s) {
    s.wifiState = nano_->online() ? ws : "UNKNOWN";
    s.wifiSsid = jStr(wifi, "ssid", "");
    s.wifiIp = jStr(wifi, "ip", "");
    s.sdMounted = jBool(jObj(st, "sd"), "mounted", false);
    s.nfcReady = jBool(jObj(st, "nfc"), "ready", false);
  });
  if (panel_) {
    PanelService::Net n = PanelService::Net::kFailed;
    if (!nano_->online()) {
      n = PanelService::Net::kFailed;
    } else if (ws == "OFF") {
      n = PanelService::Net::kOff;
    } else if (ws == "CONNECTING") {
      n = PanelService::Net::kConnecting;
    } else if (ws == "CONNECTED") {
      n = PanelService::Net::kConnected;
    }
    panel_->setNetwork(n);
  }
}

void Application::muteAlarm() {
  alarm_->mute();
  if (!lastVoiceJob_.empty() && nano_ && nano_->online()) {
    JsonValue p = JsonValue::makeObject();
    p.set("job_id", lastVoiceJob_);
    nano_->request(vp::kOpStopVoice, p, nullptr);
  }
  state_.update([](SystemState& s) { s.alarmMuted = true; });
  setMessage("报警已确认, 声音已静音");
}

void Application::unmuteAlarm() {
  alarm_->mute(false);
  state_.update([](SystemState& s) { s.alarmMuted = false; });
  setMessage("报警声已恢复");
}

void Application::playVoice(const char* asset) {
  if (!cfg_.voice.enabled || !nano_->online() || pfail_ || shutdown_) return;
  // The speaker would be in the microphone's window.
  if (measuring_ && acousticCapture_) {
    ++voiceHeld_;
    LOGI("voice", "%s held back: acoustic window open", asset);
    return;
  }
  lastVoiceJob_ = nano_->playVoice(asset, cfg_.voice.volume, [asset](const NanoReply& r) {
    if (!r.ok) LOGW("voice", "PLAY_VOICE %s refused: %s", asset, r.code.c_str());
  });
}

// ---------------------------------------------------------------------------
// Measurement, printing, light
// ---------------------------------------------------------------------------

// Why an output may not be switched on now (empty = allowed). Both outputs
// also need SW301 (hardware) and are forced off by the PL on PFAIL/watchdog.
std::string Application::outputBlocker() {
  if (pfail_ || shutdown_) return "power_fail";
  if (!board_->heavyLoadAllowed()) return "power_budget";
  if (printer_->busy()) return "printing";
  return std::string();
}

// LED1 runs at the lamp brightness; LED2 at 100 %: chopping the projector's
// supply would fight its own driver and input capacitors.
void Application::applyOutputs() {
  const SystemState s = state_.snapshot();
  if (!carrierOk_) {
    if (lampPwm_.ready()) {
      lampPwm_.set(s.lightOn, s.lightPercent);
      lampPwm_.setProjector(s.laserOn);
    }
    return;
  }
  carrier_->setPwmPercent(0, s.lightPercent);
  carrier_->setPwmPercent(1, 100);
  carrier_->setPwmEnable(s.lightOn, s.laserOn);
}

bool Application::setLight(bool on, std::string* err) {
  if (!carrierOk_ && !lampPwm_.ready()) {
    if (err) *err = "no_carrier";
    return false;
  }
  const std::string blocked = on ? outputBlocker() : std::string();
  if (!blocked.empty()) {
    if (err) *err = blocked;
    on = false;
  }
  if (!carrierOk_) {
    const SystemState s = state_.snapshot();
    if (on && !s.lightAllowed) {
      if (err) *err = "light_interlock";
      on = false;
    }
    if (!lampPwm_.set(on, s.lightPercent)) {
      if (err) *err = "light_io";
      on = false;
      lampPwm_.set(false, 0);
    }
  }
  state_.update([on](SystemState& st) { st.lightOn = on; });
  applyOutputs();
  return on;
}

bool Application::setLaser(bool on, std::string* err) {
  if (!carrierOk_ && !lampPwm_.ready()) {
    if (err) *err = "no_carrier";
    return false;
  }
  if (on && measuring_ && cfg_.laser.offDuringMeasure) {
    if (err) *err = "measuring";
    return false;
  }
  const std::string blocked = on ? outputBlocker() : std::string();
  if (!blocked.empty()) {
    if (err) *err = blocked;
    on = false;
  }
  if (!carrierOk_) {
    if (on && !state_.snapshot().lightAllowed) {
      if (err) *err = "light_interlock";
      on = false;
    }
    if (!lampPwm_.setProjector(on)) {
      if (err) *err = "projector_io";
      on = false;
      lampPwm_.setProjector(false);
    }
  }
  state_.update([on](SystemState& st) { st.laserOn = on; });
  applyOutputs();
  return on;
}

std::string Application::makeMeasurementId() {
  std::string base = cfg_.deviceId + "-";
  if (state_.snapshot().timeValid) {
    base += formatLocal(realtimeMs(), cfg_.tzOffsetMin, "%Y%m%d-%H%M%S");
  } else {
    base += "M" + std::to_string(monoMs());
  }
  std::string id = base;
  for (int n = 2; dirExists(cfg_.recordsDir + "/" + id); ++n) id = base + "-" + std::to_string(n);
  return id;
}

bool Application::startMeasurement(double durationS, bool baseline, std::string& err) {
  if (shutdown_ || pfail_) {
    err = "power";
    return false;
  }
  if (measuring_) {
    err = "already_measuring";
    return false;
  }
  if (printer_->busy()) {
    err = "printing";
    return false;
  }
  if (!engine_ || engine_->state() != MeasurementEngine::State::kPreview) {
    err = "camera_not_ready";
    return false;
  }
  MeasureConfig mc = cfg_.measure;
  if (durationS > 0) mc.durationS = durationS;
  const SystemState s = state_.snapshot();
  if (mc.method == "multipoint") {
    if (!prepareMultiPoint(mc, s, baseline, err)) return false;
  } else if (baseline) {
    err = "baseline_not_used";
    return false;
  } else {
    applyStereoScale(mc);
  }
  lightAuto_ = false;
  if (!baseline && cfg_.light.autoOnMeasure && !s.lightOn) {
    std::string lerr;
    lightAuto_ = setLight(true, &lerr);
    if (!lightAuto_) LOGW(kTag, "light not switched on: %s", lerr.c_str());
  }
  // The projected speckle stays put when the surface moves sideways and would
  // pull the tracker towards zero displacement.
  laserRestore_ = cfg_.laser.offDuringMeasure && s.laserOn;
  if (laserRestore_) setLaser(false, nullptr);
  captureLaserOff_ = laserRestore_;
  captureContext_ = state_.snapshot();  // Freeze point, illumination and UTC mapping for this record.
  captureStartMonoNs_ = monoNs();
  captureStartUtcMs_ = realtimeMs();
  const uint64_t generation = ++captureGeneration_;
  if (!engine_->beginCapture(mc, s.roiCx, s.roiCy,
      [this, generation](const MeasureResult& r) {
        loop_.post([this, generation, r]() {
          if (!shutdown_ && !pfail_ && generation == captureGeneration_ && measuring_) onMeasureDone(r);
        });
      })) {
    err = "capture_refused";
    if (lightAuto_) setLight(false, nullptr);
    if (laserRestore_) setLaser(true, nullptr);
    laserRestore_ = false;
    return false;
  }
  measuring_ = true;
  acousticRecord_ = acoustic_ && !baseline && acoustic_->status().state != AcousticService::State::kAbsent;
  acousticCapture_ = acousticRecord_;
  voiceHeld_ = 0;
  if (acousticCapture_) {
    acoustic_->beginCapture(mc.durationS);
    if (!lastVoiceJob_.empty() && nano_->online()) {  // a prompt still playing would be in the window
      JsonValue p = JsonValue::makeObject();
      p.set("job_id", lastVoiceJob_);
      nano_->request(vp::kOpStopVoice, p, nullptr);
    }
  }
  state_.update([baseline](SystemState& st) { st.baselineCapture = baseline; });
  if (cfg_.fan.pauseDuringMeasure && fanOn_) {
    board_->setFan(false);
    fanOn_ = false;
  }
  setMessage(baseline ? "采集停机基线..." : "测量中...");
  return true;
}

// mm/px from stereo depth at the ROI (route proposal section 7): Z / fx of the
// left camera at the measurement resolution. Recorded as calibration
// "stereo:<calibration id>:z<mm>" so the record shows where the scale came from.
void Application::applyStereoScale(MeasureConfig& mc) {
  stereoCapture_ = RecordMeta::Stereo();
  if (!stereo_ || !engine_) return;
  const MeasurementEngine::Live l = engine_->live();
  const std::shared_ptr<const DepthMap> d = depthLive();
  const StereoCalibration c = stereo_->calibration();
  double mmPerPx = 0, z = 0;
  const SystemState s = state_.snapshot();
  const bool ok = d && c.valid && l.frameW > 0 && l.frameH > 0 &&
                  stereoScaleAt(*d, s.roiCx, s.roiCy, 0.5 * mc.roiW / l.frameW, 0.5 * mc.roiH / l.frameH,
                                c.left.K[0] * l.frameW / c.width, mmPerPx, z);
  stereoCapture_.enabled = true;
  stereoCapture_.backend = d ? d->backend : std::string();
  stereoCapture_.calibrationId = c.valid ? c.id : std::string();
  stereoCapture_.roiDepthValid = ok;
  stereoCapture_.roiDepthMm = ok ? z : 0;
  const std::string& src = cfg_.stereo.scaleSource;
  if (src == "file" || (src == "file_or_stereo" && mc.mmPerPx > 0)) return;
  if (ok) {
    stereoCapture_.usedForScale = true;
    mc.mmPerPx = mmPerPx;
    char id[96];
    snprintf(id, sizeof(id), "stereo:%s:z%.0f", c.id.c_str(), z);
    mc.calibrationId = id;
    LOGI(kTag, "stereo scale: Z %.0f mm -> %.5f mm/px", z, mmPerPx);
  } else if (src == "stereo") {
    mc.mmPerPx = 0;  // no valid depth at the ROI: stay in pixels rather than reuse a stale file scale
    mc.calibrationId.clear();
    LOGW(kTag, "stereo scale requested but no valid depth at the ROI: result stays in pixels");
  }
}

void Application::stopMeasurement() {
  if (measuring_ && engine_) engine_->endCapture();
}

// Multi-point captures: the layout of the current box and reference group,
// and the baseline to compare with, or a new file to collect one into.
bool Application::prepareMultiPoint(MeasureConfig& mc, const SystemState& s, bool baseline, std::string& err) {
  const MeasurementEngine::Live l = engine_->live();
  const MultiPointLayout layout = currentLayout(s, l.frameW, l.frameH);
  if (!layout.error.empty()) {
    err = layout.error;
    return false;
  }
  mc.refCx = s.refCx;
  mc.refCy = s.refCy;
  const std::string& dir = mc.multipoint.baselineDir;
  if (baseline) {
    if (!makeDirs(dir)) {
      err = "storage";
      return false;
    }
    mc.baselineCapture = true;
    mc.durationS = mc.multipoint.baselineS;
    const std::string tag =
        s.timeValid ? formatLocal(realtimeMs(), cfg_.tzOffsetMin, "%Y%m%d-%H%M%S") : "M" + std::to_string(monoMs());
    mc.baselineFile = multiPointBaselinePath(dir, multiPointHash(layout), tag);
  } else {
    mc.baselineFile = findMultiPointBaseline(dir, layout, nullptr, &baselineFilesThisRun_);
    if (mc.baselineFile.empty()) {
      err = "baseline_required";
      return false;
    }
  }
  return true;
}

MultiPointLayout Application::currentLayout(const SystemState& s, int frameW, int frameH) const {
  if (frameW <= 0 || frameH <= 0) {
    MultiPointLayout l;
    l.error = "camera_not_ready";
    return l;
  }
  return multiPointLayout(frameW, frameH, boxAt(frameW, frameH, s.roiCx, s.roiCy, cfg_.measure.roiW, cfg_.measure.roiH),
                          s.refCx, s.refCy, cfg_.measure.multipoint);
}

void Application::refreshBaseline() {
  if (cfg_.measure.method != "multipoint" || !engine_) return;
  const MeasurementEngine::Live l = engine_->live();
  const MultiPointLayout layout = currentLayout(state_.snapshot(), l.frameW, l.frameH);
  const uint32_t hash = layout.error.empty() ? multiPointHash(layout) : 0;
  if (baselineScanned_ && hash == baselineHash_) return;
  int windows = 0;
  const std::string file =
      hash ? findMultiPointBaseline(cfg_.measure.multipoint.baselineDir, layout, &windows, &baselineFilesThisRun_)
           : std::string();
  baselineHash_ = hash;
  baselineScanned_ = true;
  state_.update([&](SystemState& st) {
    st.layoutError = layout.error;
    st.baselineValid = !file.empty();
    st.baselineWindows = windows;
  });
}

void Application::onMeasureDone(const MeasureResult& r) {
  measuring_ = false;
  const bool acousticRecord = acousticRecord_, acousticWindow = acousticCapture_;
  acousticRecord_ = acousticCapture_ = false;
  const int64_t now = monoMs();
  if (lightAuto_) setLight(false, nullptr);
  lightAuto_ = false;
  if (laserRestore_) setLaser(true, nullptr);
  laserRestore_ = false;
  state_.update([](SystemState& st) { st.baselineCapture = false; });
  if (r.method == "multipoint" && r.multipoint.baseline) {
    if (acoustic_) acoustic_->cancelCapture();
    if (r.valid && !r.multipoint.baselineFile.empty()) baselineFilesThisRun_.insert(r.multipoint.baselineFile);
    baselineScanned_ = false;
    refreshBaseline();
    if (r.valid) {
      setMessage("停机基线已保存 (" + std::to_string(r.multipoint.baselineWindows) + " 个窗口)");
      if (panel_) panel_->flash(PanelService::Flash::kSuccess, now);
      playVoice("baseline_done");
    } else {
      setMessage("基线未保存: " + reasonText(r.reason));
    }
    return;
  }
  const SystemState s = state_.snapshot();
  const bool cal = r.mmPerPx > 0;
  const double k = cal ? r.mmPerPx : 1.0;

  ResultSummary sum;
  sum.have = true;
  sum.valid = r.valid;
  sum.reason = r.reason;
  sum.unit = cal ? "mm" : "px";
  sum.dominantHz = r.dominantHz;
  sum.peakClear = r.peakClear;
  sum.peakToPeak = r.ppPx * k;
  sum.rms = r.rmsPx * k;
  sum.peak = r.peakAmpPx * k;
  sum.fps = r.fpsMeasured;
  sum.dropped = r.dropped;
  sum.method = r.method;
  if (r.method == "multipoint") {
    const MeasureResult::MultiPoint& mp = r.multipoint;
    sum.points = static_cast<int>(mp.points.size());
    sum.passedPoints = mp.passed;
    sum.waveformValid = mp.waveformValid;
    sum.waveformReason = mp.waveformReason;
    std::vector<double> rms;
    for (size_t i = 0; i < mp.gate.size(); ++i)
      if (mp.gate[i]) rms.push_back(mp.rmsPx[i]);
    std::sort(rms.begin(), rms.end());
    const size_t n = rms.size();
    if (n) sum.pointRmsPx = n % 2 ? rms[n / 2] : 0.5 * (rms[n / 2 - 1] + rms[n / 2]);
  }
  sum.pointId = captureContext_.pointId;
  sum.utcMs = realtimeMs();
  sum.level = AlarmService::classifyResult(sum, cfg_.alarm);
  sum.measurementId = makeMeasurementId();
  // Invalid amplitude is UNKNOWN, never a stale previous alarm/normal result.
  alarm_->setLevel(sum.level, now);
  alarm_->tick(now);  // drive outputs before record/audio processing blocks the loop
  if (panel_) panel_->setAlarm(sum.level);

  // Save first: the printed slip is generated from the saved record.
  if (pfail_) {
    setMessage("掉电预警: 本次结果未保存");
  } else if (r.frames > 0) {
    RecordMeta m;
    m.measurementId = sum.measurementId;
    m.deviceId = cfg_.deviceId;
    m.pointId = captureContext_.pointId;
    m.pointName = captureContext_.pointName;
    m.nfcUid = captureContext_.pointUid;
    m.startUtcMs = captureStartUtcMs_ + (r.startMonoNs - captureStartMonoNs_) / 1000000;
    m.timeValid = captureContext_.timeValid;
    m.timeSource = captureContext_.timeSource;
    m.tzOffsetMin = cfg_.tzOffsetMin;
    m.cameraSource = engine_->live().source;
    m.stereo = stereoCapture_;
    if (acousticRecord) collectAcoustic(r, acousticWindow, sum, m.acoustic);
    m.lightOn = captureContext_.lightOn;
    m.lightPercent = captureContext_.lightPercent;
    m.laserOn = captureContext_.laserOn;
    m.laserOffForCapture = captureLaserOff_;
    m.unit = sum.unit;
    m.level = sum.level;
    m.warnPp = cfg_.alarm.warnPp;
    m.alarmPp = cfg_.alarm.alarmPp;
    m.envValid = s.envValid;
    m.tempC = s.tempC;
    m.rhPercent = s.rhPercent;
    m.distanceValid = s.distanceValid;
    m.distanceMm = s.distanceMm;
    m.appVersion = VIBMON_APP_VERSION;
    m.plVersion = carrierOk_ ? "carrier_ctrl 1.0" : "";
    m.nanoVersion = jStr(nano_->helloData(), "nano_version", "");
    std::vector<ImuSample> imu;
    std::vector<AdxlSample> adxl;
    const int64_t t1 = r.startMonoNs + static_cast<int64_t>(r.durationS * 1e9);
    sensors_->imuWindow(r.startMonoNs, t1, imu);
    sensors_->adxlWindow(r.startMonoNs, t1, adxl);
    std::string err;
    sum.saved = saveRecord(cfg_.recordsDir, m, r, imu, adxl, err);
    if (sum.saved) {
      lastRecordId_ = sum.measurementId;
    } else {
      LOGE(kTag, "saving %s failed: %s", sum.measurementId.c_str(), err.c_str());
      if (panel_) panel_->flash(PanelService::Flash::kFailure, now);
      playVoice("storage_error");
    }
  }

  if (acoustic_) acoustic_->cancelCapture();  // also when nothing was saved

  if (r.valid) {
    char buf[128];
    if (r.method == "multipoint") {
      snprintf(buf, sizeof(buf), "主频 %.2f Hz, %d/%d 测点超过停机基线", sum.dominantHz, sum.passedPoints, sum.points);
    } else if (!sum.peakClear) {
      snprintf(buf, sizeof(buf), "无明确主频, 峰峰值 %.4g %s, %s", sum.peakToPeak, sum.unit.c_str(), levelText(sum.level));
    } else {
      snprintf(buf, sizeof(buf), "主频 %.2f Hz, 峰峰值 %.4g %s, %s", sum.dominantHz, sum.peakToPeak, sum.unit.c_str(),
               levelText(sum.level));
    }
    setMessage(buf);
    if (sum.saved || pfail_) {
      playVoice(sum.level == AlarmLevel::kAlarm ? "vibration_alarm"
                                                : (sum.level == AlarmLevel::kWarn ? "vibration_warn" : "measure_done"));
    }
    if (panel_ && sum.saved) panel_->flash(PanelService::Flash::kSuccess, now);
  } else {
    setMessage("测量无效: " + r.reason);
  }
  state_.update([&](SystemState& st) {
    st.last = sum;
    st.alarmLevel = alarm_->level();
    st.alarmMuted = alarm_->muted();
  });
}

// The audio of the capture window, analysed against the camera's frequency.
void Application::collectAcoustic(const MeasureResult& r, bool window, ResultSummary& sum, RecordMeta::Acoustic& a) {
  a.enabled = true;
  a.saveWav = cfg_.acoustic.saveWav;
  a.bandLowHz = cfg_.acoustic.service.analysis.bandLowHz;
  a.bandHighHz = cfg_.acoustic.service.analysis.bandHighHz;
  a.fanOn = captureContext_.fanOn;
  a.buzzer = cfg_.alarm.buzzer && !captureContext_.alarmMuted &&
             (captureContext_.alarmLevel == AlarmLevel::kWarn || captureContext_.alarmLevel == AlarmLevel::kAlarm);
  a.voiceSuppressed = voiceHeld_;
  sum.acousticEnabled = true;
  const AcousticService::Status st = acoustic_->status();
  a.source = st.source;
  a.lr = st.lr;
  if (!window) {
    a.reason = st.state == AcousticService::State::kFault ? st.error : std::string("not_ready");
    sum.acousticReason = a.reason;
    return;
  }
  const int64_t t1 = r.startMonoNs + static_cast<int64_t>(r.durationS * 1e9);
  AcousticService::Window w = acoustic_->endCapture(r.startMonoNs, t1);
  if (!w.available) {
    a.reason = st.state == AcousticService::State::kFault ? st.error : w.reason;
    sum.acousticReason = a.reason;
    return;
  }
  a.available = true;
  a.sampleRate = w.sampleRate;
  a.pdmClockHz = w.pdmClockHz;
  a.firstIndex = w.firstIndex;
  a.startOffsetMs = (w.firstNs - r.startMonoNs) * 1e-6;
  a.lr = w.lr;
  a.source = w.source;
  // Independent estimate: the camera's frequency (a clear one) only enters the comparison.
  a.analysis = analyzeAcoustic(w.pcm.data(), w.pcm.size(), w.sampleRate, r.valid && r.peakClear ? r.dominantHz : 0,
                               cfg_.acoustic.service.analysis);
  a.pcm.swap(w.pcm);
  sum.acousticValid = a.analysis.valid;
  sum.acousticReason = a.analysis.valid ? std::string() : a.analysis.reason;
  sum.acousticRelation = a.analysis.relation;
  sum.acousticHz = a.analysis.acousticHz;
  sum.acousticHarmonic = a.analysis.harmonic;
  LOGI(kTag, "acoustic: %s, envelope %.3f Hz (%s), %.1f dBFS", a.analysis.valid ? "valid" : a.analysis.reason.c_str(),
       a.analysis.acousticHz, a.analysis.relation.c_str(), a.analysis.rmsDbfs);
}

bool Application::printRecord(const std::string& idIn, std::string& err) {
  const std::string id = idIn.empty() ? (lastRecordId_.empty() ? latestRecordId(cfg_.recordsDir) : lastRecordId_) : idIn;
  if (id.empty()) {
    err = "no_record";
    return false;
  }
  if (pfail_ || shutdown_) {
    err = "power";
    return false;
  }
  if (measuring_) {
    err = "measuring";
    return false;
  }
  if (!board_->heavyLoadAllowed()) {
    err = "power_budget";
    return false;
  }
  {
    const SystemState st = state_.snapshot();
    if (st.lightOn) setLight(false, nullptr);
    if (st.laserOn) setLaser(false, nullptr);
  }
  JsonValue rec;
  ReportData d;
  if (!loadRecord(cfg_.recordsDir, id, rec, d.t, d.disp, d.f, d.amp)) {
    err = "record_unreadable";
    return false;
  }
  const JsonValue& res = jObj(rec, "result");
  d.deviceId = jStr(rec, "device_id", cfg_.deviceId);
  d.measurementId = id;
  const std::string local = jStr(rec, "start_local", "");
  if (local.empty()) {
    d.timeText = "时间未校准";
  } else {
    const int tz = static_cast<int>(jInt(rec, "timezone_offset_min", cfg_.tzOffsetMin));
    char tzs[24];
    snprintf(tzs, sizeof(tzs), " (UTC%+d)", tz / 60);
    d.timeText = local + tzs;
  }
  d.pointText = jStr(rec, "point_id", "") + " " + jStr(rec, "point_name", "");
  d.summary.valid = jBool(res, "valid", false);
  d.summary.reason = jStr(res, "reason", "");
  d.summary.unit = jStr(res, "unit", "px");
  const bool mm = d.summary.unit == "mm";
  d.summary.dominantHz = jNum(res, "dominant_frequency_hz", 0);
  d.summary.peakClear = jBool(res, "peak_clear", true);
  d.summary.peakToPeak = jNum(res, mm ? "displacement_peak_to_peak_mm" : "displacement_peak_to_peak_px", 0);
  d.summary.rms = jNum(res, mm ? "displacement_rms_mm" : "displacement_rms_px", 0);
  d.summary.peak = jNum(res, mm ? "spectral_peak_mm" : "spectral_peak_px", 0);
  const std::string lv = jStr(res, "level", "normal");
  d.summary.level = lv == "alarm" ? AlarmLevel::kAlarm :
                    (lv == "warn" ? AlarmLevel::kWarn : (lv == "normal" ? AlarmLevel::kNormal : AlarmLevel::kUnknown));
  const JsonValue& cam = jObj(rec, "camera");
  d.fps = jNum(cam, "fps_measured", 0);
  d.dropped = static_cast<uint32_t>(jInt(cam, "frames_dropped", 0));
  readMultiPointSummary(res, d.summary);
  const JsonValue& cal = jObj(rec, "calibration");
  d.calibrationText = mm ? jStr(cal, "id", "已标定") : std::string("未标定 (像素)");
  const JsonValue& env = jObj(rec, "environment");
  if (jBool(env, "valid", false)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "环境 %.1f °C  %.0f %%RH", jNum(env, "temperature_c", 0), jNum(env, "humidity_percent", 0));
    d.envText = buf;
  }
  // Keep the spectrum plot to the analysed band.
  while (!d.f.empty() && d.f.back() > d.fps * 0.4 && d.fps > 0) {
    d.f.pop_back();
    d.amp.pop_back();
  }
  // The 384-dot slip shows about ten periods of the waveform, not all samples.
  const bool periodic = d.summary.valid && d.summary.peakClear && d.summary.dominantHz > 0;
  const double window = periodic ? 10.0 / d.summary.dominantHz : 2.0;
  if (!d.t.empty() && d.t.back() - d.t.front() > window) {
    const double t0 = d.t.front();
    size_t n = 0;
    while (n < d.t.size() && d.t[n] - t0 <= window) ++n;
    d.t.resize(n);
    d.disp.resize(n);
  }
  Canvas c = renderReport(d, cfg_.printer.widthDots);
  PrintJob job;
  job.id = id;
  job.bits = toMonoBits(c, job.widthBytes);
  job.height = c.height();
  if (!printer_->submit(std::move(job), err)) return false;
  if (panel_) panel_->setPrinting(true);
  state_.update([](SystemState& s) { s.printerState = "starting"; });
  setMessage("打印中...");
  return true;
}

void Application::onPrintDone(bool ok, const std::string& id, const std::string& error) {
  if (panel_) {
    panel_->setPrinting(false);
    panel_->flash(ok ? PanelService::Flash::kSuccess : PanelService::Flash::kFailure, monoMs());
  }
  if (ok) {
    setMessage(id == "printer-selftest" ? "打印机自检供电窗口结束" : "打印完成: " + id);
  } else {
    setMessage("打印失败: " + error);
    playVoice("print_failed");
  }
}

// ---------------------------------------------------------------------------
// Power fail and shutdown
// ---------------------------------------------------------------------------

void Application::powerFail(bool active) {
  if (active == pfail_) return;
  pfail_ = active;
  state_.update([active](SystemState& s) { s.pfail = active; });
  if (acoustic_) {
    acoustic_->setPowerFail(active);
    if (active) acoustic_->cancelCapture();
  }
  if (active) acousticCapture_ = false;
  if (active) {
    LOGW(kTag, "PFAIL: 12 V below threshold - loads off, no new prints or writes");
    setLight(false, nullptr);  // the PL already forced both outputs low
    setLaser(false, nullptr);
    laserRestore_ = false;
    board_->inhibitPrinter(true);
    if (printer_) printer_->abort("power_fail");
    ++captureGeneration_;
    if (measuring_) engine_->cancel();
    measuring_ = false;
    alarm_->suspend(true);
    board_->allLoadsOff();
    fanOn_ = false;
    setMessage("电源掉电预警");
  } else {
    LOGI(kTag, "PFAIL released");
    carrier_->clearPwmKills();
    if (!shutdown_) board_->inhibitPrinter(false);
    if (!shutdown_) alarm_->suspend(false);
    setMessage("电源恢复");
  }
}

void Application::requestShutdown() {
  if (shutdown_) return;
  shutdown_ = true;
  ++captureGeneration_;
  LOGI(kTag, "shutdown requested");
  setMessage("正在准备关机...");
  if (measuring_) engine_->cancel();
  measuring_ = false;
  acousticCapture_ = false;
  if (acoustic_) acoustic_->setPowerFail(true);  // clock off before the sensor supply goes
  board_->inhibitPrinter(true);
  printer_->abort("shutdown");
  setLight(false, nullptr);
  setLaser(false, nullptr);
  laserRestore_ = false;
  alarm_->suspend(true);
  board_->allLoadsOff();
  fanOn_ = false;
  // Finish after the Nano confirmed PREPARE_POWER_OFF, or after 1.5 s at most.
  auto done = std::make_shared<bool>(false);
  auto finish = [this, done]() {
    if (*done) return;
    *done = true;
    finishShutdown();
  };
  if (nano_->online()) {
    JsonValue p = JsonValue::makeObject();
    p.set("reason", "operator_shutdown");
    nano_->request(vp::kOpPreparePowerOff, p, [finish](const NanoReply&) { finish(); });
  }
  loop_.addTimer(1500, finish, false);
}

void Application::finishShutdown() {
  if (printer_ && printer_->busy()) {
    loop_.addTimer(50, [this]() { finishShutdown(); }, false);
    return;
  }
  if (engine_) engine_->stopCamera();
  sync();
  if (panel_) {
    panel_->prepareShutdown();
    panel_->tick(monoMs());
  }
  state_.update([](SystemState& s) { s.shutdownPrepared = true; });
  setMessage(cfg_.poweroffOnShutdown ? "应用已停止, 等待Linux关机" : "应用已停止, Linux仍在运行");
  LOGI(kTag, "application prepared; OS shutdown %s", cfg_.poweroffOnShutdown ? "pending" : "disabled");
  if (cfg_.poweroffOnShutdown) {
    loop_.addTimer(1500, [this]() {
      if (std::system("/sbin/poweroff") != 0) LOGW(kTag, "poweroff command failed");
      requestExit();
    }, false);
  }
}

void Application::requestExit() {
  loop_.post([this]() {
    if (exiting_) return;
    exiting_ = true;
    shutdown_ = true;
    ++captureGeneration_;
    measuring_ = false;
    if (engine_) engine_->cancel();
    if (board_) board_->inhibitPrinter(true);
    if (printer_) printer_->abort("exit");
    if (carrierOk_) {
      carrier_->setPwmEnable(false, false);
      {
        carrier_->setWatchdog(0);  // orderly exit: no fault frame
        const uint32_t off[5] = {0, 0, 0, 0, 0};
        carrier_->setLeds(off);
        carrier_->sendLeds();
      }
    }
    if (board_ && board_->tcaReady()) board_->allLoadsOff();
    if (lampPwm_.ready()) {
      lampPwm_.setProjector(false);
      lampPwm_.set(false, 0);
    }
    loop_.stop();
  });
}

void Application::setMessage(const std::string& msg) {
  LOGI(kTag, "%s", msg.c_str());
  const int64_t now = monoMs();
  state_.update([&](SystemState& s) {
    s.message = msg;
    s.messageMs = now;
  });
}

// ---------------------------------------------------------------------------
// Periodic work
// ---------------------------------------------------------------------------

void Application::every100ms() {
  const int64_t now = monoMs();
  if (lampPwm_.ready()) {
    const bool healthy = lampPwm_.tick();
    const SystemState s = state_.snapshot();
    if ((s.lightOn || s.laserOn) && (!healthy || !s.lightAllowed)) {
      // Clear both requested states together before another output update.
      // Sequential setLight/setLaser could briefly restore the other channel
      // after the heartbeat has already recovered in the PL.
      lampPwm_.setProjector(false);
      lampPwm_.set(false, 0);
      state_.update([](SystemState& st) { st.lightOn = false; st.laserOn = false; });
      setMessage("补光/投影已关闭：请检查照明许可、供电或控制心跳");
    }
  }
  if (engine_) {
    MeasurementEngine::Live l = engine_->live();
    state_.update([&](SystemState& s) {
      s.measureState = measureStateName(l.state);
      s.measureElapsedS = l.elapsedS;
      s.measureDurationS = l.durationS;
      s.cameraOk = l.cameraOk;
    });
    if (panel_) {
      const SystemState s = state_.snapshot();
      PanelService::Meas m = PanelService::Meas::kIdle;
      if (l.state == MeasurementEngine::State::kCapturing) {
        m = ((s.lightOn || s.laserOn) && !s.lightAllowed) ? PanelService::Meas::kConditionBad
                                                            : PanelService::Meas::kCapturing;
      } else if (l.state == MeasurementEngine::State::kAnalyzing) {
        m = PanelService::Meas::kComputing;
      } else if (!l.cameraOk) {
        m = PanelService::Meas::kConditionBad;
      }
      panel_->setMeasure(m);
    }
  }
  if (alarm_) alarm_->tick(now);
  if (panel_) {
    const SystemState s = state_.snapshot();
    const bool fault = !s.tcaOk || s.pfail || !s.fault.empty();
    if (!shutdown_) panel_->setSystem(fault ? PanelService::Sys::kFault : PanelService::Sys::kOk);
    panel_->tick(now);
  }
}

void Application::every200ms() {
  if (board_ && i2cMgmt_.isOpen()) {
    // The carrier interrupt bridge may be absent. Refresh the physical light
    // interlock even when no expander interrupt reaches the application.
    board_->pollInputs();
    board_->samplePower();
  }
  if (acoustic_) {
    const AcousticService::Status a = acoustic_->status();
    state_.update([&a](SystemState& s) {
      s.acousticState = AcousticService::stateName(a.state);
      s.acousticError = a.error;
      s.acousticLr = a.lr;
      s.acousticLevelDbfs = a.levelDbfs;
      s.acousticRate = a.sampleRate;
    });
  }
  if (carrierOk_) {
    tickClock_.addPair(carrier_->timestamp(), monoNs());
    // PFAIL release needs the input high for a full second.
    if (pfail_ && ((carrier_->inputs() >> kSrcPfailN) & 1u) != 0) {
      if (pfailHighSinceMs_ == 0) pfailHighSinceMs_ = monoMs();
      if (monoMs() - pfailHighSinceMs_ >= 1000) powerFail(false);
    } else if (pfail_) {
      pfailHighSinceMs_ = 0;
    }
  }
}

void Application::every1s() {
  const int64_t now = monoMs();
  refreshBaseline();
  updateFromNanoStatus();
  if (nano_->online() && now - lastTimeSyncMs_ > 600000 && jBool(nano_->lastStatus(), "time_valid", false)) {
    lastTimeSyncMs_ = now;
    syncTimeFromNano();
  }
  if (board_ && board_->tcaReady() && !pfail_ && !shutdown_) {
    // Fan policy; a paused fan avoids airflow on the target while measuring.
    const SystemState s = state_.snapshot();
    bool want = fanOn_;
    if (cfg_.fan.mode == "on") {
      want = true;
    } else if (cfg_.fan.mode == "off") {
      want = false;
    } else if (s.envValid) {
      if (s.tempC >= cfg_.fan.onTempC) want = true;
      if (s.tempC <= cfg_.fan.offTempC) want = false;
    }
    if (measuring_ && cfg_.fan.pauseDuringMeasure) want = false;
    if (want != fanOn_ && board_->setFan(want)) fanOn_ = want;
  }
  // Tachometer: no pulse for 2 s means no valid speed (never keep the last value).
  double hz = 0;
  bool valid = false;
  if (!tachPeriods_.empty() && now - lastTachMs_ < 2000) {
    double sum = 0;
    for (double p : tachPeriods_) sum += p;
    hz = tachPeriods_.size() / sum;
    valid = true;
  } else {
    tachPeriods_.clear();
    lastTachTicks_ = 0;
  }
  const NanoLink::Stats& ns = nano_->stats();
  const std::string nanoVer = jStr(nano_->helloData(), "nano_version", "");
  const std::string camSrc = engine_ ? engine_->live().source : std::string();
  const unsigned long crc = static_cast<unsigned long>(nano_->decoderStats().crcErrors);
  state_.update([&](SystemState& s) {
    s.tachHz = hz;
    s.tachValid = valid;
    if (now - s.messageMs > 15000) s.message.clear();
    s.fanOn = fanOn_;
    s.nanoRequests = static_cast<unsigned long>(ns.requests);
    s.nanoRetries = static_cast<unsigned long>(ns.retries);
    s.nanoTimeouts = static_cast<unsigned long>(ns.timeouts);
    s.nanoCrcErrors = crc;
    s.nanoVersion = nanoVer;
    s.cameraSource = camSrc;
  });
  if (engine_ && engine_->state() == MeasurementEngine::State::kStopped && !shutdown_ && now >= cameraRetryMs_) {
    cameraRetryMs_ = now + 5000;
    if (engine_->startCamera()) LOGI(kTag, "camera started");
  }
}

MeasurementEngine::Live Application::cameraLive() {
  return engine_ ? engine_->live() : MeasurementEngine::Live();
}

void Application::setDisplayNeeds(bool preview, bool vision, bool depth) {
  if (engine_) engine_->setDisplayNeeds(preview, vision);
  if (!stereoStarted_) return;
  const bool run = depth || cfg_.stereo.scaleSource != "file";
  if (run && stereo_->paused()) {
    // The time without depth is no publish interval.
    std::lock_guard<std::mutex> lock(depthMutex_);
    depthPublishedNs_ = depthIntervalNs_ = 0;
  }
  stereo_->setPaused(!run);
  engine_->setFrameTapRate(run ? cfg_.stereo.service.rateHz : 0);
}

void Application::publishDepth(std::shared_ptr<const DepthMap> depth) {
  std::lock_guard<std::mutex> lock(depthMutex_);
  const int64_t now = monoNs();
  // Measured from publishing, not from the frame time: on the PS a pair can
  // take seconds, which would make every map stale on arrival.
  if (depthPublishedNs_ > 0) depthIntervalNs_ = now - depthPublishedNs_;
  depthPublishedNs_ = now;
  depth_ = std::move(depth);
}

std::shared_ptr<const DepthMap> Application::depthLive() {
  std::lock_guard<std::mutex> lock(depthMutex_);
  const int64_t limit = std::max<int64_t>(3000000000LL, depthIntervalNs_ * 5 / 2);
  if (depth_ && monoNs() - depthPublishedNs_ > limit) return nullptr;
  return depth_;
}

// ---------------------------------------------------------------------------
// Operator settings
// ---------------------------------------------------------------------------

void Application::publishSettings() {
  UserSettings u;
  u.durationS = cfg_.measure.durationS;
  u.roiW = cfg_.measure.roiW;
  u.roiH = cfg_.measure.roiH;
  u.roiMaxPx = roiMaxPx(cfg_.measure);
  u.axis = cfg_.measure.axis;
  u.autoLight = cfg_.light.autoOnMeasure;
  u.warnPp = cfg_.alarm.warnPp;
  u.alarmPp = cfg_.alarm.alarmPp;
  u.binaryThreshold = cfg_.alarm.binaryThreshold;
  u.buzzer = cfg_.alarm.buzzer;
  u.voice = cfg_.voice.enabled;
  u.volume = cfg_.voice.volume;
  u.fanMode = cfg_.fan.mode;
  u.ledBrightness = cfg_.panel.brightness;
  u.visionLowHz = cfg_.vision.lowHz;
  u.visionHighHz = cfg_.vision.highHz;
  u.visionGain = cfg_.vision.gain;
  u.laserOffDuringMeasure = cfg_.laser.offDuringMeasure;
  state_.update([&u](SystemState& s) { s.settings = u; });
}

JsonValue Application::settingsJson() {
  const SystemState s = state_.snapshot();
  const UserSettings& u = s.settings;
  JsonValue o = JsonValue::makeObject();
  o.set("duration_s", u.durationS);
  o.set("roi_width_px", u.roiW);
  o.set("roi_height_px", u.roiH);
  o.set("axis", std::string(1, u.axis));
  o.set("auto_light", u.autoLight);
  o.set("light_percent", s.lightPercent);
  o.set("laser_off_during_measurement", u.laserOffDuringMeasure);
  o.set("warn_pp", u.warnPp);
  o.set("alarm_pp", u.alarmPp);
  o.set("buzzer", u.buzzer);
  o.set("voice", u.voice);
  o.set("volume", u.volume);
  o.set("fan_mode", u.fanMode);
  o.set("led_brightness", u.ledBrightness);
  o.set("vision_low_hz", u.visionLowHz);
  o.set("vision_high_hz", u.visionHighHz);
  o.set("vision_gain", u.visionGain);
  return o;
}

// Validates every given key first; nothing changes when one of them is bad.
bool Application::applySettings(const JsonValue& c, std::string& err) {
  const SystemState s = state_.snapshot();
  double duration = cfg_.measure.durationS;
  int roiW = cfg_.measure.roiW, roiH = cfg_.measure.roiH;
  char axis = cfg_.measure.axis;
  bool autoLight = cfg_.light.autoOnMeasure;
  double pct = s.lightPercent;
  bool laserOff = cfg_.laser.offDuringMeasure;
  double warn = cfg_.alarm.warnPp, alarm = cfg_.alarm.alarmPp;
  bool buzzer = cfg_.alarm.buzzer, voice = cfg_.voice.enabled;
  int volume = cfg_.voice.volume, led = cfg_.panel.brightness, gain = cfg_.vision.gain;
  std::string fan = cfg_.fan.mode;
  double vlo = cfg_.vision.lowHz, vhi = cfg_.vision.highHz;
  bool ok = true;
  if (c.find("duration_s")) {
    duration = jNum(c, "duration_s", -1);
    ok = ok && duration >= 1 && duration <= 600;
  }
  if (c.find("roi_width_px")) roiW = static_cast<int>(jInt(c, "roi_width_px", -1));
  if (c.find("roi_height_px")) roiH = static_cast<int>(jInt(c, "roi_height_px", -1));
  const int maxPx = roiMaxPx(cfg_.measure);
  ok = ok && roiW >= kRoiMinPx && roiW <= maxPx && roiH >= kRoiMinPx && roiH <= maxPx;
  if (c.find("axis")) {
    const std::string a = jStr(c, "axis", "");
    // A loaded calibration fixes the axis its scale belongs to.
    ok = ok && (a == "x" || a == "y") && !(cfg_.measure.mmPerPx > 0 && a[0] != cfg_.measure.axis);
    if (!a.empty()) axis = a[0];
  }
  if (c.find("auto_light")) autoLight = jBool(c, "auto_light", autoLight);
  if (c.find("light_percent")) {
    pct = jNum(c, "light_percent", -1);
    ok = ok && pct >= 0 && pct <= 100;
  }
  if (c.find("laser_off_during_measurement")) laserOff = jBool(c, "laser_off_during_measurement", laserOff);
  if (c.find("warn_pp")) warn = jNum(c, "warn_pp", -1);
  if (c.find("alarm_pp")) alarm = jNum(c, "alarm_pp", -1);
  ok = ok && warn > 0 && alarm > 0 && (cfg_.alarm.binaryThreshold || alarm > warn);
  if (c.find("buzzer")) buzzer = jBool(c, "buzzer", buzzer);
  if (c.find("voice")) voice = jBool(c, "voice", voice);
  if (c.find("volume")) {
    volume = static_cast<int>(jInt(c, "volume", -1));
    ok = ok && volume >= 0 && volume <= 100;
  }
  if (c.find("fan_mode")) {
    fan = jStr(c, "fan_mode", "");
    ok = ok && (fan == "auto" || fan == "on" || fan == "off");
  }
  if (c.find("led_brightness")) {
    led = static_cast<int>(jInt(c, "led_brightness", -1));
    ok = ok && led >= 0 && led <= 255;
  }
  if (c.find("vision_low_hz")) vlo = jNum(c, "vision_low_hz", -1);
  if (c.find("vision_high_hz")) vhi = jNum(c, "vision_high_hz", -1);
  ok = ok && vlo > 0 && vhi > vlo && vhi <= 200;
  if (c.find("vision_gain")) {
    gain = static_cast<int>(jInt(c, "vision_gain", -1));
    ok = ok && gain >= 1 && gain <= 200;
  }
  if (!ok) {
    err = "invalid_value";
    return false;
  }
  const bool captureKeys = c.find("duration_s") || c.find("roi_width_px") || c.find("roi_height_px") ||
                           c.find("axis") || c.find("laser_off_during_measurement");
  if (captureKeys && measuring_) {
    err = "measuring";
    return false;
  }
  const bool alarmChanged = cfg_.alarm.warnPp != warn || cfg_.alarm.alarmPp != alarm || cfg_.alarm.buzzer != buzzer;
  cfg_.measure.durationS = duration;
  cfg_.measure.roiW = roiW;
  cfg_.measure.roiH = roiH;
  cfg_.measure.axis = axis;
  cfg_.light.autoOnMeasure = autoLight;
  cfg_.light.percent = pct;
  cfg_.laser.offDuringMeasure = laserOff;
  cfg_.alarm.warnPp = warn;
  cfg_.alarm.alarmPp = alarm;
  cfg_.alarm.buzzer = buzzer;
  cfg_.voice.enabled = voice;
  cfg_.voice.volume = volume;
  cfg_.fan.mode = fan;
  cfg_.panel.brightness = led;
  cfg_.vision.lowHz = vlo;
  cfg_.vision.highHz = vhi;
  cfg_.vision.gain = gain;
  state_.update([pct](SystemState& st) { st.lightPercent = pct; });
  if (s.lightOn && (carrierOk_ || lampPwm_.ready())) applyOutputs();  // new brightness takes effect now
  if (engine_) engine_->setVision(cfg_.vision);
  if (alarmChanged) {
    const AlarmLevel level = AlarmService::classifyResult(s.last, cfg_.alarm);
    if (alarm_->level() != level) alarm_->setLevel(level, monoMs());
    alarm_->tick(monoMs());
    if (panel_) panel_->setAlarm(level);
    state_.update([&](SystemState& st) {
      if (st.last.have) st.last.level = level;
      st.alarmLevel = level;
      st.alarmMuted = alarm_->muted();
    });
  }
  publishSettings();
  if (!saveSettings()) LOGW(kTag, "settings not saved to %s", cfg_.settingsFile.c_str());
  return true;
}

bool Application::saveSettings() {
  if (cfg_.settingsFile.empty()) return true;
  JsonValue root = JsonValue::makeObject();
  JsonValue m = JsonValue::makeObject();
  m.set("duration_s", cfg_.measure.durationS);
  m.set("roi_width_px", cfg_.measure.roiW);
  m.set("roi_height_px", cfg_.measure.roiH);
  m.set("roi_center_x", cfg_.roiCenterX);
  m.set("roi_center_y", cfg_.roiCenterY);
  m.set("axis", std::string(1, cfg_.measure.axis));
  JsonValue ref = JsonValue::makeObject();  // multi-point reference group: a baseline belongs to the layout
  ref.set("x", cfg_.measure.refCx);
  ref.set("y", cfg_.measure.refCy);
  m.set("reference_roi", ref);
  root.set("measurement", m);
  JsonValue l = JsonValue::makeObject();
  l.set("auto_on_measure", cfg_.light.autoOnMeasure);
  l.set("percent", cfg_.light.percent);
  root.set("light", l);
  JsonValue lz = JsonValue::makeObject();
  lz.set("off_during_measurement", cfg_.laser.offDuringMeasure);
  root.set("laser", lz);
  JsonValue a = JsonValue::makeObject();
  a.set("warn_peak_to_peak", cfg_.alarm.warnPp);
  a.set("alarm_peak_to_peak", cfg_.alarm.alarmPp);
  a.set("buzzer", cfg_.alarm.buzzer);
  a.set("binary_threshold", cfg_.alarm.binaryThreshold);
  root.set("alarm", a);
  JsonValue v = JsonValue::makeObject();
  v.set("enabled", cfg_.voice.enabled);
  v.set("volume", cfg_.voice.volume);
  root.set("voice", v);
  JsonValue f = JsonValue::makeObject();
  f.set("mode", cfg_.fan.mode);
  root.set("fan", f);
  JsonValue p = JsonValue::makeObject();
  p.set("brightness", cfg_.panel.brightness);
  root.set("panel", p);
  JsonValue vis = JsonValue::makeObject();
  vis.set("low_hz", cfg_.vision.lowHz);
  vis.set("high_hz", cfg_.vision.highHz);
  vis.set("gain", cfg_.vision.gain);
  root.set("vision", vis);
  const size_t slash = cfg_.settingsFile.rfind('/');
  if (slash != std::string::npos && slash > 0 && !makeDirs(cfg_.settingsFile.substr(0, slash))) return false;
  return writeFileAtomic(cfg_.settingsFile, jsonPretty(root));
}

// ---------------------------------------------------------------------------
// Commands (UI and control socket)
// ---------------------------------------------------------------------------

JsonValue Application::command(const JsonValue& cmd) {
  return loop_.call<JsonValue>([this, cmd]() { return handleCommand(cmd); });
}

JsonValue Application::stateJson() {
  const SystemState s = state_.snapshot();
  JsonValue o = JsonValue::makeObject();
  o.set("init_done", s.initDone);
  o.set("carrier_ok", s.carrierOk);
  o.set("tca_ok", s.tcaOk);
  o.set("ina_ok", s.inaOk);
  o.set("rtc_ok", s.rtcOk);
  JsonValue p = JsonValue::makeObject();
  p.set("bus_v", s.power.busV);
  p.set("current_a", s.power.currentA);
  p.set("power_w", s.power.powerW);
  p.set("warn", s.powerWarn);
  p.set("deny_heavy_loads", s.powerDeny);
  p.set("over_current_latched", s.overCurrentLatched);
  p.set("pfail", s.pfail);
  o.set("power", p);
  JsonValue l = JsonValue::makeObject();
  l.set("allowed_by_sw301", s.lightAllowed);
  l.set("pwm_ready", carrierOk_ || lampPwm_.ready());
  l.set("backend", carrierOk_ ? "carrier_ctrl" : (lampPwm_.ready() ? "j301_gpio_pwm" : "absent"));
  l.set("on", s.lightOn);
  l.set("percent", s.lightPercent);
  o.set("light", l);
  JsonValue lz = JsonValue::makeObject();
  lz.set("on", s.laserOn);
  lz.set("off_during_measurement", s.settings.laserOffDuringMeasure);
  o.set("laser", lz);
  JsonValue t = JsonValue::makeObject();
  t.set("valid", s.timeValid);
  t.set("source", s.timeSource);
  t.set("utc", formatUtcIso(realtimeMs()));
  o.set("time", t);
  JsonValue n = JsonValue::makeObject();
  n.set("online", s.nanoOnline);
  n.set("wifi", s.wifiState);
  n.set("ssid", s.wifiSsid);
  n.set("ip", s.wifiIp);
  n.set("sd_mounted", s.sdMounted);
  n.set("nfc_ready", s.nfcReady);
  const NanoLink::Stats& ns = nano_->stats();
  n.set("requests", static_cast<unsigned long>(ns.requests));
  n.set("retries", static_cast<unsigned long>(ns.retries));
  n.set("timeouts", static_cast<unsigned long>(ns.timeouts));
  n.set("events", static_cast<unsigned long>(ns.events));
  n.set("event_gaps", static_cast<unsigned long>(ns.gaps));
  n.set("crc_errors", static_cast<unsigned long>(nano_->decoderStats().crcErrors));
  o.set("nano", n);
  JsonValue pt = JsonValue::makeObject();
  pt.set("uid", s.pointUid);
  pt.set("id", s.pointId);
  pt.set("name", s.pointName);
  o.set("point", pt);
  JsonValue sen = JsonValue::makeObject();
  sen.set("imu_ok", s.imuOk);
  sen.set("adxl345_ok", s.adxlOk);
  sen.set("tof", s.tofNote);
  sen.set("distance_mm", s.distanceValid ? JsonValue(s.distanceMm) : JsonValue());
  sen.set("temperature_c", s.envValid ? JsonValue(s.tempC) : JsonValue());
  sen.set("humidity_percent", s.envValid ? JsonValue(s.rhPercent) : JsonValue());
  sen.set("tach_hz", s.tachValid ? JsonValue(s.tachHz) : JsonValue());
  o.set("sensors", sen);
  JsonValue m = JsonValue::makeObject();
  m.set("state", s.measureState);
  m.set("elapsed_s", s.measureElapsedS);
  m.set("camera_ok", s.cameraOk);
  if (engine_) {
    const auto live = engine_->live();
    m.set("camera_source", live.source);
    m.set("frame_width", live.frameW);
    m.set("frame_height", live.frameH);
    m.set("frames", static_cast<unsigned long>(live.frameCount));
    m.set("fps", live.fps);
    m.set("proc_ms", live.procMs);
    JsonValue vision = JsonValue::makeObject();
    vision.set("backend", live.vision.backend);
    vision.set("width", live.vision.w);
    vision.set("height", live.vision.h);
    vision.set("fps", live.vision.fps);
    vision.set("proc_ms", live.vision.procMs);
    vision.set("settled", live.vision.settled);
    vision.set("paused", live.vision.paused);
    m.set("vision", vision);
  }
  JsonValue sj = JsonValue::makeObject();
  sj.set("enabled", stereo_ != nullptr);
  if (stereo_) {
    const StereoService::Stats ss = stereo_->stats();
    sj.set("calibrated", ss.calibrated);
    sj.set("calibration_id", ss.calibrationId);
    sj.set("error", ss.error);
    sj.set("pairs", static_cast<unsigned long>(ss.pairs));
    sj.set("skew_rejected", static_cast<unsigned long>(ss.skewRejected));
    sj.set("published", static_cast<unsigned long>(ss.published));
    sj.set("last_skew_ms", ss.lastSkewMs);
    sj.set("last_proc_ms", ss.lastProcMs);
    sj.set("last_valid_fraction", ss.lastValid);
    sj.set("backend", ss.backend);
    sj.set("pl_error", ss.plError);
    sj.set("pl_frames", static_cast<unsigned long>(ss.plFrames));
    sj.set("pl_failures", static_cast<unsigned long>(ss.plFailures));
    sj.set("pl_verified", static_cast<unsigned long>(ss.plVerified));
    sj.set("pl_mismatch_frames", static_cast<unsigned long>(ss.plMismatchFrames));
    sj.set("pl_cycles", static_cast<unsigned long>(ss.plCycles));
  }
  sj.set("scale_source", cfg_.stereo.scaleSource);
  m.set("stereo", sj);
  JsonValue ac = JsonValue::makeObject();
  ac.set("enabled", acoustic_ != nullptr);
  if (acoustic_) {
    const AcousticService::Status a = acoustic_->status();
    ac.set("state", AcousticService::stateName(a.state));
    ac.set("error", a.error.empty() ? JsonValue() : JsonValue(a.error));
    ac.set("source", a.source);
    ac.set("lr", a.lr.empty() ? JsonValue() : JsonValue(a.lr));
    ac.set("pdm_clock_hz", a.pdmClockHz);
    ac.set("sample_rate_hz", a.sampleRate);
    ac.set("level_dbfs", a.levelDbfs > -199 ? JsonValue(a.levelDbfs) : JsonValue());
    ac.set("samples", static_cast<unsigned long long>(a.samples));
    ac.set("gaps", static_cast<unsigned long>(a.gaps));
    ac.set("buffered_s", a.bufferedS);
  }
  m.set("acoustic", ac);
  m.set("roi_center_x", s.roiCx);
  m.set("roi_center_y", s.roiCy);
  m.set("zoom", s.zoom);
  m.set("zoom_center_x", s.zoomCx);
  m.set("zoom_center_y", s.zoomCy);
  m.set("method", s.method);
  if (s.method == "multipoint") {
    JsonValue mp = JsonValue::makeObject();
    mp.set("reference_center_x", s.refCx);
    mp.set("reference_center_y", s.refCy);
    mp.set("layout_error", s.layoutError);
    mp.set("baseline_valid", s.baselineValid);
    mp.set("baseline_windows", s.baselineWindows);
    mp.set("baseline_capture", s.baselineCapture);
    if (engine_) {
      const MeasurementEngine::Live live = engine_->live();
      mp.set("windows", live.mpWindows);
      mp.set("window_status", multiPointStatusName(live.mpWindow.status));
      mp.set("window_frequency_hz", live.mpWindow.status == 1 ? JsonValue(live.mpWindow.frequencyHz) : JsonValue());
      mp.set("window_passed", live.mpWindow.passed);
    }
    m.set("multipoint", mp);
  }
  if (s.last.have) {
    JsonValue r = JsonValue::makeObject();
    r.set("measurement_id", s.last.measurementId);
    r.set("valid", s.last.valid);
    r.set("reason", s.last.reason);
    r.set("unit", s.last.unit);
    r.set("dominant_hz", s.last.dominantHz);
    r.set("peak_clear", s.last.peakClear);
    const bool amplitudeOk = s.last.valid && (s.last.method != "multipoint" || s.last.waveformValid);
    r.set("waveform_valid", amplitudeOk);
    r.set("waveform_reason", s.last.waveformReason);
    r.set("peak_to_peak", amplitudeOk ? JsonValue(s.last.peakToPeak) : JsonValue());
    r.set("rms", amplitudeOk ? JsonValue(s.last.rms) : JsonValue());
    r.set("level", levelText(s.last.level));
    r.set("saved", s.last.saved);
    if (s.last.acousticEnabled) {
      JsonValue a = JsonValue::makeObject();
      a.set("valid", s.last.acousticValid);
      a.set("reason", s.last.acousticReason);
      a.set("relation", s.last.acousticRelation);
      a.set("frequency_hz", s.last.acousticHz > 0 ? JsonValue(s.last.acousticHz) : JsonValue());
      a.set("harmonic", s.last.acousticHarmonic);
      r.set("acoustic", a);
    }
    m.set("last", r);
  }
  o.set("measurement", m);
  o.set("alarm_level", levelText(s.alarmLevel));
  o.set("alarm_muted", s.alarmMuted);
  o.set("alarm_output_ok", alarm_->outputsOk());
  o.set("printer", s.printerState);
  o.set("printer_error", s.printerError);
  o.set("message", s.message);
  o.set("shutdown_prepared", s.shutdownPrepared);
  return o;
}

JsonValue Application::handleCommand(const JsonValue& c) {
  const std::string cmd = jStr(c, "cmd", "");
  if (shutdown_ && cmd != "status" && cmd != "shutdown") return errReply("shutting_down");
  std::string err;
  if (cmd == "status") return okReply(stateJson());
  if (cmd == "settings") return okReply(settingsJson());
  if (cmd == "set") {
    if (!applySettings(c, err)) return errReply(err);
    return okReply(settingsJson());
  }
  if (cmd == "measure_start") {
    if (!startMeasurement(jNum(c, "duration_s", 0), false, err)) return errReply(err);
    return okReply();
  }
  if (cmd == "baseline_start") {
    if (!startMeasurement(0, true, err)) return errReply(err);
    return okReply();
  }
  if (cmd == "measure_stop") {
    stopMeasurement();
    return okReply();
  }
  if (cmd == "print") {
    if (!printRecord(jStr(c, "measurement_id", ""), err)) return errReply(err);
    return okReply();
  }
  if (cmd == "printer_test") {
    if (measuring_ || pfail_ || shutdown_) return errReply("measuring_or_power");
    if (!state_.snapshot().tcaOk) return errReply("no_carrier");
    setLight(false, nullptr);
    setLaser(false, nullptr);
    Canvas page(cfg_.printer.widthDots, 160, 0xffffff);
    drawText(page, fontFace(24, true), 8, 8, "见微 打印测试", 0);
    drawText(page, fontFace(18), 8, 45, "JIANWEI / AX7010", 0);
    drawText(page, fontFace(16), 8, 75, "此页仅测试打印，不是测量报告", 0);
    page.rect(8, 110, page.width()-16, 32, 0, 2);
    PrintJob job;
    job.id = "printer-test";
    job.bits = toMonoBits(page, job.widthBytes);
    job.height = page.height();
    if (jStr(c, "mode", "") == "ascii") {
      job.rawText = "AX7010 PRINT TEST\n0123456789\nABC abc\n";
      job.height = 72;
    }
    if (jStr(c, "mode", "") == "selftest") {
      job.id = "printer-selftest";
      job.powerOnly = true;
      job.height = 0;
    }
    if (!printer_->submit(std::move(job), err)) return errReply(err);
    state_.update([](SystemState& s) { s.printerState = "starting"; });
    setMessage("正在打印测试页");
    return okReply();
  }
  if (cmd == "light") {
    if (c.find("percent")) {
      const double p = std::max(0.0, std::min(100.0, jNum(c, "percent", 100)));
      state_.update([p](SystemState& s) { s.lightPercent = p; });
    }
    const bool on = jBool(c, "on", state_.snapshot().lightOn);
    const bool now = setLight(on, &err);
    if (on && !now) return errReply(err);
    return okReply();
  }
  if (cmd == "laser") {
    const bool on = jBool(c, "on", !state_.snapshot().laserOn);
    const bool now = setLaser(on, &err);
    if (on && !now) return errReply(err);
    return okReply();
  }
  if (cmd == "mute") {
    // {"on":false} undoes it (the app's mute button is a toggle).
    if (jBool(c, "on", true))
      muteAlarm();
    else
      unmuteAlarm();
    return okReply();
  }
  if (cmd == "roi") {
    // Centre x/y in normalised image coordinates; w/h (pixels) also resize the
    // box and are kept like the roi_width_px/roi_height_px settings.
    if (c.find("w") || c.find("h")) {
      JsonValue size = JsonValue::makeObject();
      size.set("roi_width_px", static_cast<int>(jInt(c, "w", cfg_.measure.roiW)));
      size.set("roi_height_px", static_cast<int>(jInt(c, "h", cfg_.measure.roiH)));
      if (!applySettings(size, err)) return errReply(err);
    }
    // ref_x/ref_y place the multi-point reference group.
    state_.update([&c](SystemState& s) {
      s.roiCx = std::max(0.0, std::min(1.0, jNum(c, "x", s.roiCx)));
      s.roiCy = std::max(0.0, std::min(1.0, jNum(c, "y", s.roiCy)));
      s.refCx = std::max(0.0, std::min(1.0, jNum(c, "ref_x", s.refCx)));
      s.refCy = std::max(0.0, std::min(1.0, jNum(c, "ref_y", s.refCy)));
    });
    const SystemState s = state_.snapshot();
    cfg_.roiCenterX = s.roiCx;
    cfg_.roiCenterY = s.roiCy;
    cfg_.measure.refCx = s.refCx;
    cfg_.measure.refCy = s.refCy;
    if (!saveSettings()) LOGW(kTag, "settings not saved to %s", cfg_.settingsFile.c_str());
    refreshBaseline();
    return okReply();
  }
  if (cmd == "zoom") {
    const int steps = static_cast<int>(jInt(c, "steps", 0));
    state_.update([steps](SystemState& s) { s.zoom = std::max(1.0, std::min(8.0, s.zoom * std::pow(1.25, steps))); });
    return okReply();
  }
  if (cmd == "pan") {
    // Centre of the zoomed live view (normalised), kept where the view fits the image.
    state_.update([&c](SystemState& s) {
      const double h = 0.5 / std::max(1.0, s.zoom);
      s.zoomCx = std::max(h, std::min(1.0 - h, jNum(c, "x", s.zoomCx)));
      s.zoomCy = std::max(h, std::min(1.0 - h, jNum(c, "y", s.zoomCy)));
    });
    return okReply();
  }
  if (cmd == "stereo_distance") {
    // A target at a known distance at (x, y) of the depth map: the right image
    // is moved until its disparity there gives that distance, which removes
    // the module's lens-centring offset; kept in the stereo calibration file.
    if (!stereo_) return errReply("stereo_off");
    const std::shared_ptr<const DepthMap> d = depthLive();
    if (!d) return errReply("no_depth");
    const double mm = jNum(c, "distance_mm", 0);
    double offset = 0;
    if (!distanceOffsetPx(*d, stereo_->calibration().width, jNum(c, "x", 0.5), jNum(c, "y", 0.5), mm, offset, err))
      return errReply(err);
    if (std::fabs(offset) > 80) return errReply("offset_too_large");  // wrong point or distance
    if (!stereo_->correctOffset(offset, d->calibrationVersion, std::to_string(static_cast<int>(mm)), err))
      return errReply(err);
    char buf[96];
    snprintf(buf, sizeof(buf), "双目深度已按 %.2f m 校正 (右图平移 %.1f px)", mm / 1000, offset);
    setMessage(buf);
    JsonValue r = JsonValue::makeObject();
    r.set("offset_px", offset);
    return okReply(r);
  }
  if (cmd == "zoom_reset") {
    state_.update([](SystemState& s) {
      s.zoom = 1;
      s.zoomCx = s.zoomCy = 0.5;
    });
    return okReply();
  }
  if (cmd == "voice") {
    const std::string asset = jStr(c, "asset", "measure_done");
    if (measuring_) return errReply("measuring");
    if (pfail_ || shutdown_) return errReply("power_restricted");
    if (!nano_->online()) return errReply("nano_offline");
    lastVoiceJob_ = nano_->playVoice(asset, static_cast<int>(jInt(c, "volume", cfg_.voice.volume)), nullptr);
    return okReply();
  }
  if (cmd == "set_net") {
    if (!nano_->online()) return errReply("nano_offline");
    JsonValue p = JsonValue::makeObject();
    p.set("ssid", jStr(c, "ssid", ""));
    p.set("password", jStr(c, "password", ""));
    p.set("persist", jBool(c, "persist", true));
    nano_->request(vp::kOpSetNet, p, [](const NanoReply& r) {
      LOGI("app", "SET_NET: %s", r.ok ? "accepted" : r.code.c_str());
    });
    return okReply();
  }
  if (cmd == "nano_status") return okReply(nano_->lastStatus());
  if (cmd == "rtc_set") {
    int64_t ms = 0;
    if (!c.getInt("unix_ms", ms) || ms < 1700000000000LL) return errReply("bad unix_ms");
    applyTime(ms, "manual");
    return okReply();
  }
  if (cmd == "set_point") {
    const std::string id = jStr(c, "id", "");
    const std::string name = jStr(c, "name", "");
    state_.update([&](SystemState& s) {
      s.pointId = id;
      s.pointName = name;
      s.pointUid.clear();
    });
    return okReply();
  }
  if (cmd == "shutdown") {
    requestShutdown();
    return okReply();
  }
  return errReply("unknown command");
}

int Application::run() {
  loop_.run();
  if (sensors_) sensors_->stop();
  if (engine_) engine_->stopCamera();
  return exitCode_;
}

}  // namespace vm
