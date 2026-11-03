#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hw/uio.h"

namespace vm {

// MP34DT01 PDM microphone receiver in the PL (pdm_mic.v): PDM clock and
// phase set-up, and the FIFO of raw PDM words (32 bits each, 16 us at 2 MHz;
// 8192 words = 131 ms). Decimation is on the PS (measure/pdm_model).
class PdmMic {
 public:
  explicit PdmMic(RegIo& io) : io_(io) {}

  // ID, major version and the word format of the core.
  bool probe(std::string& err);
  uint32_t clockHz();  // the core's system clock as built (CLK_HZ)

  struct Setup {
    uint32_t halfDiv = 0;   // 0: the divider closest to a 2 MHz PDM clock
    uint32_t sampleAt = 0;  // 0: three quarters into the half period
    bool phaseHigh = false; // L/R = VDD (data while the clock is high)
  };
  // Configures and enables the clock; the core restarts with an empty FIFO.
  void start(const Setup& s);
  void stop();
  void clear();  // empties the FIFO and clears OVERFLOW/DROPPED
  uint32_t halfDiv();

  struct Status {
    bool running = false, overflow = false, killed = false;
    uint32_t level = 0;
  };
  Status status();

  // One poll: WORDS latches the FIFO level, DROPPED, the partial word and
  // the clock counter together; the words in the FIFO at that instant are
  // read. A FIFO already frozen by an overflow before the latch is empty
  // after the reads and is released; one that overflows later keeps its
  // words for the next poll, so words from both sides of a gap never join.
  struct Snapshot {
    uint32_t words = 0;    // words completed since enable, dropped ones included
    uint32_t level = 0, bits = 0, dropped = 0;
    uint32_t ts = 0;       // clock counter at the latch (about when the newest bit was taken)
    uint32_t first() const { return words - dropped - level; }  // index of the first word read
  };
  Snapshot read(std::vector<uint32_t>& out);
  uint32_t now();  // the core's clock counter

 private:
  RegIo& io_;
};

}  // namespace vm
