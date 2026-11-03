#pragma once
#include <Arduino.h>

#include <deque>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// Voice playback: SD WAV -> I2S (standard I2S, 32-bit stereo slots, mono copied
// to both channels) -> MAX98357A. AMP_EN is raised only after the DMA has been
// primed with silence and dropped only after the tail has played out.
enum class JobState : uint8_t { kQueued, kPlaying, kDone, kStopped, kFailed };
const char* jobStateName(JobState s);

struct AudioJob {
  std::string jobId;
  std::string assetId;
  std::string path;
  int volume = 0;
  JobState state = JobState::kQueued;
  std::string reason;
  uint32_t underruns = 0;
  uint32_t playedMs = 0;
};

enum class EnqueueResult : uint8_t { kQueued, kDuplicate, kBusy, kNoDriver };

class AudioService {
 public:
  bool begin();
  bool driverOk() const { return driverOk_; }

  EnqueueResult enqueue(const std::string& jobId, const std::string& assetId, const std::string& path, int volume,
                        AudioJob& snapshot);
  // Stops or cancels one job; returns false for an unknown job_id.
  bool stop(const std::string& jobId, AudioJob& snapshot);
  // Cancels everything. failure=true reports AUDIO_FAILED, otherwise AUDIO_DONE.
  void stopAll(const char* reason, bool failure);
  // New protocol session: stop everything and forget previous job ids.
  void resetSession();

  bool find(const std::string& jobId, AudioJob& snapshot) const;
  bool current(AudioJob& snapshot) const;  // playing job, else first queued
  bool busy() const;

 private:
  struct PlayResult {
    bool ok = false;
    bool stopped = false;
    bool failure = false;
    std::string reason;
    uint32_t underruns = 0;
    uint32_t playedMs = 0;
  };

  static void taskEntry(void* arg);
  void taskLoop();
  PlayResult play(const AudioJob& job);
  void writeSilence(uint32_t frames);
  AudioJob* findLocked(const std::string& jobId);
  void finishLocked(AudioJob& job, JobState state, const std::string& reason, bool failure);
  void pruneHistoryLocked();
  bool waitIdle(uint32_t timeoutMs) const;

  mutable SemaphoreHandle_t mutex_ = nullptr;
  SemaphoreHandle_t wake_ = nullptr;
  TaskHandle_t task_ = nullptr;
  bool driverOk_ = false;
  std::deque<AudioJob> history_;
  std::deque<std::string> queue_;
  std::string playingId_;
  volatile bool playing_ = false;
  volatile bool stopRequested_ = false;
  bool stopFailure_ = false;
  std::string stopReason_;

  uint8_t readBuf_[2048];
  int32_t outBuf_[1024];  // 512 stereo frames
};

extern AudioService g_audio;
