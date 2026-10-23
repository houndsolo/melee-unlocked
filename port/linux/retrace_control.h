// Cooperative control at existing VI boundaries; never unwinds or re-enters game.run.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "host_services.h"
#include "process_checkpoint.h"
#include <functional>
#include <array>
#include <ostream>

namespace linux_host {
// Restricted to ordinary two-player retail VS matches. External IDs, not FtKind/GrKind.
MuMatchOverride parse_fixed_match(const std::string& value);
void parse_stock_rules(const std::string& value, int32_t& seconds, int32_t& stocks);
bool parse_rng_seed_boundary(const std::string& value);
void write_observation(std::ostream&, const MuStatePod&, const Counters&);

class RetraceControl {
 public:
  RetraceControl(int socket_fd, uint32_t boot_limit, std::function<void()> flush = {},
                 std::function<std::vector<uint8_t>()> replay = {},
                 std::function<RecordingStatus()> recording_status = {},
                 bool include_preroll = false);
  ~RetraceControl();
  RetraceControl(const RetraceControl&) = delete;
  RetraceControl& operator=(const RetraceControl&) = delete;
  // false means quit/EOF; true resumes the existing game stack. Before ready,
  // the boot script supplies input. Afterwards pads replace all four ports.
  bool boundary(const MuStatePod&, const Counters&);
  bool ready() const { return ready_; }
  const std::array<MuPadStatus, 4>& pads() const { return pads_; }
 private:
  void send(const std::string&);
  bool receive(std::string&);
  std::function<std::vector<uint8_t>()> replay_;
  std::function<RecordingStatus()> recording_status_;
  ProcessCheckpoint checkpoint_;
  std::function<void()> flush_;
  int fd_;
  uint32_t boot_limit_;
  bool ready_ = false;
  bool include_preroll_ = false;
  std::array<MuPadStatus, 4> pads_{};
};
}
