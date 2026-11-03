#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vm {

// Analysis of a mono PCM window of the acoustic reference (MP34DT01 on J706):
// level and clipping, the strongest tone 100 Hz..10 kHz, the envelope
// spectrum of a carrier band (machines modulate their noise at the rotation
// frequency) and direct low-frequency peaks, and how the envelope frequency
// relates to the one the camera measured. dBFS: a full-scale sine is 0 dBFS.
// None of it is a displacement, a velocity or a sound pressure level.
struct AcousticConfig {
  // Candidate carrier bands: the envelope peak that stands out most decides
  // the band (without looking at the camera's frequency). Bands above
  // 0.45 x the sample rate are cut or skipped.
  std::vector<std::array<double, 2>> carrierBands = {{200, 1000}, {1000, 3000}, {3000, 8000}, {1000, 8000}};
  double bandLowHz = 10, bandHighHz = 100;  // peak search, envelope and direct
  double minProminenceDb = 8;               // floor of the peak power over the band median (see acoustic.cpp)
  double minDurationS = 2;
};

struct AcousticPeak {
  double hz = 0, prominenceDb = 0;
};

struct AcousticAnalysis {
  bool valid = false;
  std::string reason;  // when not valid: too_short | stuck | no_signal | clipped
  double sampleRate = 0, durationS = 0;
  double rmsDbfs = -200, peakDbfs = -200;
  double dcFraction = 0, clipFraction = 0;  // mean / full scale, share of samples at full scale
  double modulation = 0;                    // envelope standard deviation / mean (chosen band)
  double carrierLowHz = 0, carrierHighHz = 0;  // chosen carrier band
  struct Band {
    double lowHz = 0, highHz = 0, modulation = 0;
    AcousticPeak top;  // hz = 0: no peak
  };
  std::vector<Band> bands;                                // every candidate band
  std::vector<AcousticPeak> envelopePeaks, directPeaks;  // strongest first
  AcousticPeak tone;                                     // hz = 0: no clear tone
  std::vector<double> envFreq, envAmp;  // envelope spectrum 0..bandHigh, amplitude relative to its peak
  // Envelope frequency against the camera's: agree | harmonic | differ |
  // no_visual | no_peak (empty when the audio is not valid).
  std::string relation;
  double visualHz = 0, acousticHz = 0, diffHz = 0, toleranceHz = 0;
  int harmonic = 0;  // acoustic = harmonic x visual; negative: visual = -harmonic x acoustic
};

AcousticAnalysis analyzeAcoustic(const int16_t* pcm, size_t n, double fs, double visualHz, const AcousticConfig& cfg);

}  // namespace vm
