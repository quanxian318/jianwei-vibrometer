#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "app/acoustic_service.h"
#include "app/alarm_service.h"
#include "app/app_api.h"
#include "app/board_service.h"
#include "app/config.h"
#include "app/control_server.h"
#include "app/panel_service.h"
#include "app/printer_service.h"
#include "app/sensor_service.h"
#include "app/state.h"
#include "app/storage.h"
#include "drv/carrier_ctrl.h"
#include "drv/lamp_pwm.h"
#include "hw/i2c_bus.h"
#include "hw/serial_port.h"
#include "hw/spi_dev.h"
#include "hw/uio.h"
#include "link/nano_link.h"
#include "measure/depth.h"
#include "measure/session.h"
#include "util/event_loop.h"
#include "util/json_util.h"

namespace vm {

#define VIBMON_APP_VERSION "0.2.0"

class SerialChannel : public ByteChannel {
 public:
  explicit SerialChannel(SerialPort& p) : p_(p) {}
  ssize_t readSome(uint8_t* b, size_t n) override { return p_.readSome(b, n); }
  ssize_t writeSome(const uint8_t* b, size_t n) override { return p_.writeSome(b, n); }

 private:
  SerialPort& p_;
};

class FbUi;

// vibmond: owns the devices and services and runs the main event loop.
// Commands from the touch UI and the control socket are executed on the loop
// thread through command(), so both paths behave identically.
class Application {
 public:
  explicit Application(const AppConfig& cfg);
  ~Application();
  bool init();
  int run();
  void requestExit();

  JsonValue command(const JsonValue& cmd);  // any thread
  SystemState snapshot() { return state_.snapshot(); }
  MeasurementEngine::Live cameraLive();
  const AppConfig& config() const { return cfg_; }
  // Stereo depth: the stereo module publishes, the UI reads (any thread).
  // The last map stays until no new one came for max(3 s, 2.5 x the recent
  // publish interval); then depth is reported as missing.
  void publishDepth(std::shared_ptr<const DepthMap> depth);
  void applyStereoScale(MeasureConfig& mc);
  void collectAcoustic(const MeasureResult& r, bool window, ResultSummary& sum, RecordMeta::Acoustic& a);
  std::shared_ptr<const DepthMap> depthLive();
  bool stereoOn() const { return stereoStarted_; }
  // What the screen shows (UI thread): the camera preview, the vision chain's
  // output, stereo depth. Work for what is not shown stops; depth keeps
  // running while the measurement takes its scale from it.
  void setDisplayNeeds(bool preview, bool vision, bool depth);

 private:
  JsonValue handleCommand(const JsonValue& cmd);
  void openDevices();
  void setupCarrierEvents();
  void pumpCarrierEvents();
  void onCarrierEvent(const CarrierEvent& e);
  void onNanoEvent(const NanoEvent& e);
  void updateFromNanoStatus();
  void syncTimeFromNano();
  void applyTime(int64_t unixMs, const char* source);
  void loadRtcTime();

  // baseline: collect a motor-off multi-point baseline instead of measuring.
  bool startMeasurement(double durationS, bool baseline, std::string& err);
  bool prepareMultiPoint(MeasureConfig& mc, const SystemState& s, bool baseline, std::string& err);
  MultiPointLayout currentLayout(const SystemState& s, int frameW, int frameH) const;
  // Looks up the baseline of the current multi-point layout (when it changed).
  void refreshBaseline();
  void stopMeasurement();
  void onMeasureDone(const MeasureResult& r);
  bool printRecord(const std::string& id, std::string& err);
  void onPrintDone(bool ok, const std::string& id, const std::string& error);
  // LED1 lamps (PWM) and LED2 laser projector (100 % duty), see applyOutputs().
  bool setLight(bool on, std::string* err);
  bool setLaser(bool on, std::string* err);
  std::string outputBlocker();
  void applyOutputs();
  void muteAlarm();
  void unmuteAlarm();
  void playVoice(const char* asset);
  void powerFail(bool active);
  void requestShutdown();
  void finishShutdown();
  void setMessage(const std::string& msg);
  void every100ms();
  void every200ms();
  void every1s();
  std::string makeMeasurementId();
  JsonValue stateJson();
  // Operator settings ("set" command, settings page).
  bool applySettings(const JsonValue& c, std::string& err);
  bool saveSettings();
  void publishSettings();
  JsonValue settingsJson();
  // Phone app (app_remote.cpp): LAN API, Nano gateway publishing, APP_* events.
  void startAppApi();
  JsonValue liveJson(bool wave);
  void publishToNano(bool force);
  void publishTopic(const char* topic, const JsonValue& data);
  void onAppEvent(const NanoEvent& e);
  void commandResult(int64_t cid, const JsonValue& reply);

  AppConfig cfg_;
  EventLoop loop_;
  StateStore state_;
  TickClock tickClock_;

  UioDevice uio_;
  std::unique_ptr<CarrierCtrl> carrier_;
  LampPwm lampPwm_;
  LinuxI2cBus i2cMgmt_, i2cSens_;
  LinuxSpiDevice spiImu_, spiAdxl_;
  SerialPort nanoPort_;
  std::unique_ptr<SerialChannel> nanoChan_;
  std::unique_ptr<NanoLink> nano_;
  std::unique_ptr<BoardService> board_;
  std::unique_ptr<PanelService> panel_;
  std::unique_ptr<AlarmService> alarm_;
  std::unique_ptr<SensorService> sensors_;
  std::unique_ptr<PrinterService> printer_;
  std::unique_ptr<MeasurementEngine> engine_;
  std::unique_ptr<StereoService> stereo_;
  std::atomic<bool> stereoStarted_{false};
  RecordMeta::Stereo stereoCapture_;  // stereo context frozen at capture start
  std::unique_ptr<AcousticService> acoustic_;
  bool acousticRecord_ = false;   // the running capture records the acoustic reference (a microphone is connected)
  bool acousticCapture_ = false;  // the running capture has an acoustic window
  int voiceHeld_ = 0;             // prompts not played during it
  std::unique_ptr<ControlServer> ctl_;
  std::unique_ptr<AppApi> appApi_;
  bool gatewayCapable_ = false;     // the Nano announced app_gateway in HELLO
  std::string publishedResultId_;   // last measurement sent to the gateway
  std::string publishedSettings_;
  int64_t nextStatusPublishMs_ = 0;
  std::string lastLiveSig_, publishedSession_;
  std::unique_ptr<FbUi> ui_;

  bool carrierOk_ = false;
  std::atomic<bool> pfail_{false};
  int64_t pfailHighSinceMs_ = 0;
  bool lightAuto_ = false;
  bool laserRestore_ = false;     // laser switched off for the running capture
  bool captureLaserOff_ = false;  // recorded with the capture
  std::atomic<bool> measuring_{false};
  SystemState captureContext_;
  int64_t captureStartMonoNs_ = 0, captureStartUtcMs_ = 0;
  uint64_t captureGeneration_ = 0;
  std::atomic<bool> shutdown_{false};
  bool exiting_ = false;
  bool fanOn_ = false;
  int exitCode_ = 0;
  uint64_t lastTachTicks_ = 0;
  int64_t lastTachMs_ = 0;
  std::deque<double> tachPeriods_;
  std::string lastRecordId_;
  uint32_t baselineHash_ = 0;     // layout of the last baseline lookup
  bool baselineScanned_ = false;  // false: look up again
  std::set<std::string> baselineFilesThisRun_;  // never implicitly reuse a previous setup's baseline
  std::string lastVoiceJob_;
  int64_t lastTimeSyncMs_ = -1000000000;
  int64_t cameraRetryMs_ = 0;
  int64_t nanoOfflineSinceMs_ = 0;
  std::mutex depthMutex_;
  std::shared_ptr<const DepthMap> depth_;
  int64_t depthPublishedNs_ = 0, depthIntervalNs_ = 0;
};

}  // namespace vm
