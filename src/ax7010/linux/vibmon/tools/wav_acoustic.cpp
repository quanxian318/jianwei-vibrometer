// wav_acoustic - the acoustic reference analysis (measure/acoustic) on a
// 16-bit PCM WAV: a record's acoustic.wav, or the sound track of a video
// (ffmpeg -i clip.mp4 -vn -ac 1 -c:a pcm_s16le clip.wav).
//
//   wav_acoustic <file.wav> [--visual Hz] [--carrier lo hi] [--band lo hi]
//                [--start s] [--duration s]
// --carrier replaces the candidate carrier bands by one band.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "measure/acoustic.h"
#include "util/wav.h"

using namespace vm;

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <file.wav> [--visual Hz] [--carrier lo hi] [--band lo hi] [--start s] [--duration s]\n",
            argv[0]);
    return 2;
  }
  AcousticConfig cfg;
  double visual = 0, start = 0, duration = 0;
  for (int i = 2; i < argc; ++i) {
    const auto arg = [&](int k) { return i + k < argc ? std::atof(argv[i + k]) : 0.0; };
    if (!strcmp(argv[i], "--visual")) {
      visual = arg(1);
      i += 1;
    } else if (!strcmp(argv[i], "--carrier")) {
      cfg.carrierBands = {{arg(1), arg(2)}};
      i += 2;
    } else if (!strcmp(argv[i], "--band")) {
      cfg.bandLowHz = arg(1);
      cfg.bandHighHz = arg(2);
      i += 2;
    } else if (!strcmp(argv[i], "--start")) {
      start = arg(1);
      i += 1;
    } else if (!strcmp(argv[i], "--duration")) {
      duration = arg(1);
      i += 1;
    }
  }
  std::vector<int16_t> pcm;
  int rate = 0;
  std::string err;
  if (!readWavMono16(argv[1], pcm, rate, err)) {
    fprintf(stderr, "%s: %s\n", argv[1], err.c_str());
    return 1;
  }
  size_t first = static_cast<size_t>(start * rate), count = duration > 0 ? static_cast<size_t>(duration * rate) : pcm.size();
  if (first > pcm.size()) first = pcm.size();
  if (count > pcm.size() - first) count = pcm.size() - first;
  const AcousticAnalysis a = analyzeAcoustic(pcm.data() + first, count, rate, visual, cfg);
  printf("%s: %d Hz, %.2f s, rms %.1f dBFS, peak %.1f dBFS, clipped %.4f%%\n", argv[1], rate, a.durationS, a.rmsDbfs,
         a.peakDbfs, 100 * a.clipFraction);
  if (!a.valid) {
    printf("not valid: %s\n", a.reason.c_str());
    return 3;
  }
  printf("carrier bands:");
  for (const AcousticAnalysis::Band& b : a.bands)
    printf(" %.0f..%.0f Hz %.3f Hz (%.1f dB);", b.lowHz, b.highHz, b.top.hz, b.top.prominenceDb);
  printf("\nchosen %.0f..%.0f Hz, envelope modulation %.3f, peaks:", a.carrierLowHz, a.carrierHighHz, a.modulation);
  for (const AcousticPeak& p : a.envelopePeaks) printf(" %.3f Hz (%.1f dB)", p.hz, p.prominenceDb);
  printf("\ndirect peaks:");
  for (const AcousticPeak& p : a.directPeaks) printf(" %.3f Hz (%.1f dB)", p.hz, p.prominenceDb);
  printf("\nstrongest tone: %.1f Hz (%.1f dB)\n", a.tone.hz, a.tone.prominenceDb);
  printf("against the camera: %s", a.relation.c_str());
  if (a.relation == "agree" || a.relation == "harmonic" || a.relation == "differ")
    printf(", acoustic %.3f Hz, visual %.3f Hz, difference %+.3f Hz (tolerance %.3f Hz, harmonic %d)", a.acousticHz,
           a.visualHz, a.diffHz, a.toleranceHz, a.harmonic);
  printf("\n");
  return 0;
}
