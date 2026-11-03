#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "app/acoustic_service.h"
#include "app/app_api.h"
#include "measure/frame_source.h"
#include "drv/pl_camera.h"
#include "ui/pl_display.h"
#include "measure/session.h"
#include "stereo/stereo_service.h"

namespace vm {

struct PointInfo {
  std::string id;
  std::string name;
};

// /etc/vibmon/vibmon.json (template: config/vibmon.json). Every key is
// optional; missing keys keep these defaults.
struct AppConfig {
  std::string deviceId = "vibmon-01";
  int tzOffsetMin = 480;

  // AXI base addresses fixed by ax7010/pl/bd/add_carrier.tcl; device nodes
  // are located through the device tree. Explicit paths override discovery.
  uint64_t carrierBase = 0x44000000, i2cMgmtBase = 0x44010000, i2cSensBase = 0x44020000;
  uint64_t uartNanoBase = 0x44040000, uartPrnBase = 0x44050000, spiSensBase = 0x44060000;
  std::string carrierDev, i2cMgmtDev, i2cSensDev, uartNanoDev, uartPrnDev, spiImuDev, spiAdxlDev;
  bool lampPwmEnabled = false;
  uint64_t lampPwmBase = 0x44070000;

  struct Nano {
    uint32_t baud = 115200;
    bool flowControl = true;
    int timeoutMs = 300;
    int statusPeriodMs = 2000;
    int offlineResetMs = 15000;
    int resetPulseMs = 100;
  } nano;

  struct Panel {
    int brightness = 32;  // 0..255 applied to every LED colour
    int countsPerDetent = 2;
    bool reverse = false;
    int debounceMs = 15;
    int longPressMs = 3000;
    int encFilterUs = 200;
    int watchdogMs = 3000;
  } panel;

  // LED1 (J301): all LED lamps, PWM dimmed.
  struct Light {
    uint32_t pwmHz = 20000;
    double percent = 100;
    bool autoOnMeasure = false;
  } light;

  // LED2 (J304): 12 V 650 nm cross-line projector, switched only (100 % duty). The
  // projected pattern is fixed in the projector's frame and does not follow an
  // in-plane surface motion, so it is switched off while displacement is tracked.
  struct Laser {
    bool offDuringMeasure = true;
  } laser;

  struct Power {
    double warnA = 4.2;
    double denyA = 4.5;
    double alertA = 4.5;
  } power;

  struct Imu {
    bool enabled = true;
    uint32_t spiHz = 1000000;
    int odrHz = 240;
    int accelG = 2;
    int gyroDps = 250;
  } imu;

  struct Adxl {
    bool enabled = false;  // enable only after the GY-291 shared-bus qualification
    uint32_t spiHz = 1000000;
    int rateCode = 0x0B;   // 200 Hz
    int rangeG = 2;
  } adxl;

  struct Tof {
    bool enabled = true;
    int timingBudgetMs = 50;
    int periodMs = 100;
    bool longRange = true;
  } tof;

  struct Sht {
    bool enabled = true;
    int periodMs = 1000;
  } sht;

  struct Printer {
    bool enabled = true;
    uint32_t baud = 9600;          // EM5820H factory default per vendor page; confirm with the self-test slip
    bool ctsFlow = true;
    int powerOnWaitMs = 2000;
    int readyTimeoutMs = 5000;
    int busyTimeoutMs = 10000;
    int minOffMs = 3000;
    std::string statusQuery = "none";  // none | dle_eot | gs_r
    int msPerRow = 2;              // print-time estimate per raster row
    int finishMarginMs = 1500;
    int stripRows = 24;
    int rasterPauseMs = 0;        // settling time after each drained raster strip
    int widthDots = 384;
    int feedLines = 4;
    int feedDots = 0;  // 0 keeps legacy line feeding; positive selects fixed dot feeding
  } printer;

  struct Camera {
    std::string source = "v4l2";  // v4l2 | pl_vdma | synthetic
    V4l2Config v4l2;
    PlCameraConfig pl;
    SyntheticConfig synthetic;
  } camera;

  MeasureConfig measure;
  double roiCenterX = 0.5, roiCenterY = 0.5;

  // Stereo depth from the second camera of the AN5642 module. Depth is
  // published only with a valid stereo calibration (pc_sim/stereo_calibrate.py).
  struct Stereo {
    bool enabled = false;
    V4l2Config right;               // right camera; size/format as the left one by default
    StereoConfig service;
    // Source of mm/px for a measurement: "file" (calibration.json only),
    // "stereo" (depth at the ROI, Z / fx), "file_or_stereo" (file first).
    std::string scaleSource = "file";
  } stereo;

  // MP34DT01 acoustic reference on J706 (PL pdm_mic.v at service.base):
  // corroborates the camera's frequency, never replaces a measurement.
  struct Acoustic {
    bool enabled = false;
    AcousticOptions service;
    bool saveWav = true;  // the measurement window as acoustic.wav in the record
  } acoustic;

  struct Alarm {
    double warnPp = 2.0;   // peak-to-peak thresholds in mm; pixels remain unclassified
    double alarmPp = 5.0;
    bool buzzer = true;
    bool binaryThreshold = false;  // strictly above alarmPp -> red; otherwise normal
  } alarm;

  struct Voice {
    bool enabled = true;
    int volume = 40;
  } voice;

  struct Fan {
    std::string mode = "auto";  // on | off | auto
    double onTempC = 40;
    double offTempC = 36;
    bool pauseDuringMeasure = true;
  } fan;

  // Display image processing (vision page): pyramid down-sampling, temporal
  // band-pass, motion magnification and band energy. Never used for metrology.
  // levels = 0: halve the camera frame until it is at most 200 pixels wide.
  VisionConfig vision;

  std::string recordsDir = "/home/root/vibmon/records";
  std::string calibrationFile = "/etc/vibmon/calibration.json";
  // Operator settings changed on the screen; loaded over this file at start.
  std::string settingsFile = "/home/root/vibmon/settings.json";

  struct Ui {
    bool enabled = true;
    std::string display = "fb";  // fb (fbdev) | pl_vdma (video path of the camera PL)
    std::string fbdev = "/dev/fb0";
    PlDisplayConfig pl;
    std::string touchDev = "auto";
    bool swapXY = false, invertX = false, invertY = false;
  } ui;

  std::string controlSocket = "/run/vibmon.sock";
  // Phone app: LAN HTTP API + UDP discovery, and publishing to the Nano
  // gateway (Wi-Fi) when the Nano firmware offers it.
  AppApiConfig appApi;
  struct Gateway {
    bool enabled = true;
    int statusPeriodMs = 2000;
  } gateway;
  bool poweroffOnShutdown = true;
  std::map<std::string, PointInfo> points;  // NFC UID (upper-case hex) -> measuring point
};

// Keys present in the file override cfg; absent keys keep their values, so
// the settings file can be applied on top of vibmon.json.
bool loadConfig(const std::string& path, AppConfig& cfg, std::string& err);
// Calibration file: {"id": "...", "mm_per_px": 0.0123, "axis": "y"}.
bool loadCalibration(const std::string& path, MeasureConfig& m, std::string& err);

}  // namespace vm
