#include "audio_service.h"

#include <FS.h>
#include <SD.h>

#include <vibproto.h>

#include "app_config.h"
#include "board_pins.h"
#include "driver/i2s.h"
#include "esp_timer.h"
#include "event_store.h"
#include "log.h"
#include "power_service.h"
#include "sd_service.h"
#include "wav_parser.h"

AudioService g_audio;

namespace {
const char* kTag = "audio";
const i2s_port_t kPort = I2S_NUM_0;
const uint32_t kChunkFrames = 512;  // frames per SD read / I2S write
const uint32_t kFadeFrames = 160;   // 10 ms at 16 kHz

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};

size_t fileReadAt(void* ctx, uint32_t offset, uint8_t* buf, size_t len) {
  File* f = static_cast<File*>(ctx);
  if (!f->seek(offset)) return 0;
  return f->read(buf, len);
}

bool ampEnable(bool on) { return g_power.setAmplifierEnabled(on); }

void postAudioEvent(const char* type, const AudioJob& job, const char* result, const std::string& reason) {
  vp::JsonValue d = vp::JsonValue::makeObject();
  d.set("job_id", job.jobId);
  d.set("asset_id", job.assetId);
  if (result) d.set("result", result);
  if (!reason.empty()) d.set("reason", reason);
  d.set("underruns", static_cast<unsigned long>(job.underruns));
  d.set("played_ms", static_cast<unsigned long>(job.playedMs));
  g_events.post(type, d);
}
}  // namespace

const char* jobStateName(JobState s) {
  switch (s) {
    case JobState::kQueued: return "QUEUED";
    case JobState::kPlaying: return "PLAYING";
    case JobState::kDone: return "DONE";
    case JobState::kStopped: return "STOPPED";
    case JobState::kFailed: return "FAILED";
  }
  return "UNKNOWN";
}

bool AudioService::begin() {
  if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
  if (wake_ == nullptr) wake_ = xSemaphoreCreateBinary();
  ampEnable(false);

  i2s_config_t config;
  memset(&config, 0, sizeof(config));
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = cfg::kDefaultSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;  // Philips I2S, MSB one BCLK after WS
  config.intr_alloc_flags = 0;
  config.dma_buf_count = cfg::kI2sDmaBufCount;
  config.dma_buf_len = cfg::kI2sDmaBufFrames;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;  // underflow plays silence instead of stale data
  config.fixed_mclk = 0;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  config.bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT;
  esp_err_t err = i2s_driver_install(kPort, &config, 0, nullptr);
  if (err == ESP_OK) {
    i2s_pin_config_t pins;
    memset(&pins, 0, sizeof(pins));
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = board_pins::gpio_raw::I2S_BCLK;
    pins.ws_io_num = board_pins::gpio_raw::I2S_WS;
    pins.data_out_num = board_pins::gpio_raw::I2S_SDOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    err = i2s_set_pin(kPort, &pins);
    if (err == ESP_OK) {
      i2s_zero_dma_buffer(kPort);
      i2s_stop(kPort);
    }
  }
  driverOk_ = (err == ESP_OK);
  if (!driverOk_) {
    LOGE(kTag, "I2S init failed: %d", static_cast<int>(err));
    return false;
  }
  if (task_ == nullptr) {
    xTaskCreatePinnedToCore(taskEntry, "audio", 6144, this, 5, &task_, 1);
  }
  return task_ != nullptr;
}

AudioJob* AudioService::findLocked(const std::string& jobId) {
  for (size_t i = 0; i < history_.size(); ++i) {
    if (history_[i].jobId == jobId) return &history_[i];
  }
  return nullptr;
}

void AudioService::pruneHistoryLocked() {
  // Keep at most kJobHistory records; only finished jobs are forgotten.
  while (history_.size() > cfg::kJobHistory) {
    bool removed = false;
    for (std::deque<AudioJob>::iterator it = history_.begin(); it != history_.end(); ++it) {
      if (it->state != JobState::kQueued && it->state != JobState::kPlaying) {
        history_.erase(it);
        removed = true;
        break;
      }
    }
    if (!removed) break;
  }
}

EnqueueResult AudioService::enqueue(const std::string& jobId, const std::string& assetId, const std::string& path,
                                    int volume, AudioJob& snapshot) {
  if (!driverOk_) return EnqueueResult::kNoDriver;
  Lock lock(mutex_);
  AudioJob* existing = findLocked(jobId);
  if (existing != nullptr) {
    snapshot = *existing;
    return EnqueueResult::kDuplicate;
  }
  if (queue_.size() >= cfg::kAudioQueueMax) return EnqueueResult::kBusy;
  AudioJob job;
  job.jobId = jobId;
  job.assetId = assetId;
  job.path = path;
  job.volume = volume;
  job.state = JobState::kQueued;
  history_.push_back(job);
  queue_.push_back(jobId);
  pruneHistoryLocked();
  snapshot = job;
  xSemaphoreGive(wake_);
  return EnqueueResult::kQueued;
}

void AudioService::finishLocked(AudioJob& job, JobState state, const std::string& reason, bool failure) {
  job.state = state;
  job.reason = reason;
  if (failure) {
    postAudioEvent("AUDIO_FAILED", job, nullptr, reason);
  } else {
    postAudioEvent("AUDIO_DONE", job, state == JobState::kDone ? "completed" : "stopped", reason);
  }
}

bool AudioService::waitIdle(uint32_t timeoutMs) const {
  uint32_t start = millis();
  while (playing_) {
    if (millis() - start > timeoutMs) return false;
    delay(5);
  }
  return true;
}

bool AudioService::stop(const std::string& jobId, AudioJob& snapshot) {
  {
    Lock lock(mutex_);
    AudioJob* job = findLocked(jobId);
    if (job == nullptr) return false;
    if (job->state == JobState::kQueued) {
      for (std::deque<std::string>::iterator it = queue_.begin(); it != queue_.end(); ++it) {
        if (*it == jobId) {
          queue_.erase(it);
          break;
        }
      }
      finishLocked(*job, JobState::kStopped, "stop_requested", false);
      snapshot = *job;
      return true;
    }
    if (job->state != JobState::kPlaying) {
      snapshot = *job;
      return true;
    }
    stopReason_ = "stop_requested";
    stopFailure_ = false;
    stopRequested_ = true;
  }
  waitIdle(300);
  Lock lock(mutex_);
  AudioJob* job = findLocked(jobId);
  if (job != nullptr) snapshot = *job;
  return true;
}

void AudioService::stopAll(const char* reason, bool failure) {
  bool waitForTask = false;
  {
    Lock lock(mutex_);
    while (!queue_.empty()) {
      AudioJob* job = findLocked(queue_.front());
      queue_.pop_front();
      if (job != nullptr) finishLocked(*job, failure ? JobState::kFailed : JobState::kStopped, reason, failure);
    }
    if (playing_) {
      stopReason_ = reason;
      stopFailure_ = failure;
      stopRequested_ = true;
      waitForTask = true;
    }
  }
  if (waitForTask && !waitIdle(500)) LOGW(kTag, "playback did not stop within 500 ms");
}

void AudioService::resetSession() {
  stopAll("session_reset", false);
  Lock lock(mutex_);
  history_.clear();
}

bool AudioService::find(const std::string& jobId, AudioJob& snapshot) const {
  Lock lock(mutex_);
  for (size_t i = 0; i < history_.size(); ++i) {
    if (history_[i].jobId == jobId) {
      snapshot = history_[i];
      return true;
    }
  }
  return false;
}

bool AudioService::current(AudioJob& snapshot) const {
  Lock lock(mutex_);
  const std::string& id = playing_ ? playingId_ : (queue_.empty() ? std::string() : queue_.front());
  if (id.empty()) return false;
  for (size_t i = 0; i < history_.size(); ++i) {
    if (history_[i].jobId == id) {
      snapshot = history_[i];
      return true;
    }
  }
  return false;
}

bool AudioService::busy() const {
  Lock lock(mutex_);
  return playing_ || !queue_.empty();
}

void AudioService::taskEntry(void* arg) { static_cast<AudioService*>(arg)->taskLoop(); }

void AudioService::taskLoop() {
  for (;;) {
    xSemaphoreTake(wake_, pdMS_TO_TICKS(1000));
    for (;;) {
      AudioJob job;
      {
        Lock lock(mutex_);
        if (queue_.empty()) break;
        AudioJob* j = findLocked(queue_.front());
        queue_.pop_front();
        if (j == nullptr) continue;
        j->state = JobState::kPlaying;
        job = *j;
        playingId_ = job.jobId;
        stopRequested_ = false;
        playing_ = true;
      }
      PlayResult r = play(job);
      {
        Lock lock(mutex_);
        AudioJob* j = findLocked(job.jobId);
        if (j != nullptr) {
          j->underruns = r.underruns;
          j->playedMs = r.playedMs;
          if (r.ok) {
            finishLocked(*j, JobState::kDone, "", false);
          } else if (r.stopped && !r.failure) {
            finishLocked(*j, JobState::kStopped, r.reason, false);
          } else {
            finishLocked(*j, JobState::kFailed, r.reason, true);
          }
        }
        playingId_.clear();
        stopRequested_ = false;
        playing_ = false;
      }
    }
  }
}

void AudioService::writeSilence(uint32_t frames) {
  memset(outBuf_, 0, sizeof(outBuf_));
  while (frames > 0) {
    uint32_t n = frames < kChunkFrames ? frames : kChunkFrames;
    size_t written = 0;
    i2s_write(kPort, outBuf_, n * 8, &written, pdMS_TO_TICKS(200));
    frames -= n;
  }
}

AudioService::PlayResult AudioService::play(const AudioJob& job) {
  PlayResult r;
  if (!g_sd.mounted()) {
    r.failure = true;
    r.reason = "sd_not_mounted";
    return r;
  }
  File f = SD.open(job.path.c_str(), FILE_READ);
  if (!f) {
    r.failure = true;
    r.reason = "open_failed";
    return r;
  }
  WavInfo info;
  WavResult wr = wavParse(fileReadAt, &f, static_cast<uint32_t>(f.size()), info);
  if (wr != kWavOk) {
    f.close();
    r.failure = true;
    r.reason = std::string("bad_wav:") + wavResultName(wr);
    return r;
  }
  if (!f.seek(info.dataOffset)) {
    f.close();
    r.failure = true;
    r.reason = "seek_failed";
    return r;
  }

  {
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("job_id", job.jobId);
    d.set("asset_id", job.assetId);
    d.set("sample_rate", static_cast<unsigned long>(info.sampleRate));
    d.set("channels", static_cast<unsigned>(info.channels));
    d.set("duration_ms", static_cast<unsigned long>(
                             (static_cast<uint64_t>(info.dataSize / info.blockAlign) * 1000) / info.sampleRate));
    g_events.post("AUDIO_STARTED", d);
  }

  i2s_set_clk(kPort, info.sampleRate, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
  i2s_zero_dma_buffer(kPort);
  i2s_start(kPort);
  const uint32_t rate = info.sampleRate;
  const uint32_t primeFrames = rate / 50;  // 20 ms of silence before the amplifier wakes
  writeSilence(primeFrames);
  if (!ampEnable(true)) {
    f.close();
    i2s_stop(kPort);
    r.failure = true;
    r.reason = "power_restricted";
    return r;
  }

  const float v = job.volume < 0 ? 0.f : (job.volume > 100 ? 1.f : job.volume / 100.f);
  const float gain = v * v * 65536.f;  // perceptual curve, PCM16 -> left-aligned 32-bit
  const uint32_t totalFrames = info.dataSize / info.blockAlign;
  uint32_t framesDone = 0;
  uint64_t framesQueued = primeFrames;
  int64_t t0 = esp_timer_get_time();
  bool fadeOut = false;

  while (framesDone < totalFrames) {
    if (stopRequested_) {
      fadeOut = true;
      r.stopped = true;
      r.failure = stopFailure_;
      r.reason = stopReason_;
    }
    if (!g_sd.present()) {
      r.failure = true;
      r.reason = "sd_removed";
      break;
    }
    uint32_t want = totalFrames - framesDone;
    if (want > kChunkFrames) want = kChunkFrames;
    if (fadeOut && want > kFadeFrames * rate / 16000) want = kFadeFrames * rate / 16000;
    if (want == 0) want = 1;
    const size_t bytes = want * info.blockAlign;
    const size_t got = f.read(readBuf_, bytes);
    if (got < info.blockAlign) {
      r.failure = true;
      r.reason = g_sd.present() ? "read_error" : "sd_removed";
      break;
    }
    const uint32_t frames = got / info.blockAlign;
    for (uint32_t i = 0; i < frames; ++i) {
      const uint8_t* p = readBuf_ + i * info.blockAlign;
      int32_t s = static_cast<int16_t>(p[0] | (p[1] << 8));
      if (info.channels == 2) s = (s + static_cast<int16_t>(p[2] | (p[3] << 8))) / 2;
      float scaled = s * gain;
      if (fadeOut) scaled *= 1.f - static_cast<float>(i + 1) / frames;
      if (scaled > 2147483520.f) scaled = 2147483520.f;
      if (scaled < -2147483648.f) scaled = -2147483648.f;
      const int32_t out = static_cast<int32_t>(scaled);
      outBuf_[2 * i] = out;
      outBuf_[2 * i + 1] = out;
    }
    // Underrun estimate: frames the DMA must have consumed by now versus frames supplied.
    const int64_t now = esp_timer_get_time();
    const uint64_t consumed = static_cast<uint64_t>(now - t0) * rate / 1000000;
    if (consumed > framesQueued) {
      r.underruns++;
      t0 = now - static_cast<int64_t>(framesQueued * 1000000 / rate);
    }
    size_t written = 0;
    esp_err_t err = i2s_write(kPort, outBuf_, frames * 8, &written, pdMS_TO_TICKS(500));
    if (err != ESP_OK || written < frames * 8) {
      r.failure = true;
      r.reason = "i2s_write_timeout";
      break;
    }
    framesQueued += frames;
    framesDone += frames;
    if (fadeOut) break;
  }
  f.close();
  // Flush with silence, let the DMA ring play out, then shut the amplifier.
  writeSilence(rate / 50);
  const uint32_t dmaMs = (cfg::kI2sDmaBufCount * cfg::kI2sDmaBufFrames * 1000) / rate;
  delay(dmaMs + 10);
  ampEnable(false);
  i2s_stop(kPort);
  r.playedMs = static_cast<uint32_t>((static_cast<uint64_t>(framesDone) * 1000) / rate);
  if (!r.failure && !r.stopped) r.ok = (framesDone == totalFrames);
  if (!r.ok && !r.stopped && !r.failure) {
    r.failure = true;
    r.reason = "incomplete";
  }
  LOGI(kTag, "job %s %s (%s) %lu ms, %lu underruns", job.jobId.c_str(), r.ok ? "done" : (r.stopped ? "stopped" : "failed"),
       r.reason.c_str(), static_cast<unsigned long>(r.playedMs), static_cast<unsigned long>(r.underruns));
  return r;
}
