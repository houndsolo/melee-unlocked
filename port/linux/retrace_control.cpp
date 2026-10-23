// SPDX-License-Identifier: GPL-3.0-or-later
#include "retrace_control.h"
#include <cerrno>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace linux_host {
namespace {
int integer(std::istream& in, int low, int high) {
  long long value;
  if (!(in >> value) || value < low || value > high)
    throw std::runtime_error("control: invalid integer or range");
  return static_cast<int>(value);
}
void end(std::istream& in) {
  in >> std::ws;
  if (!in.eof()) throw std::runtime_error("control: unexpected trailing fields");
}
bool live_two_player_match(const MuStatePod& state, bool include_preroll) {
  unsigned present = 0;
  for (const auto& player : state.player) present += player.present != 0;
  return state.scene_major == 2 && state.scene == 2 && (state.match_frame || include_preroll) && present == 2;
}
}
MuMatchOverride parse_fixed_match(const std::string& value) {
  std::istringstream in(value);
  MuMatchOverride match{};
  match.stage = integer(in, 0, 32);
  if (match.stage != 8 && match.stage != 31 && match.stage != 32)
    throw std::runtime_error("control: supported stages are Yoshi's Story, Battlefield and Final Destination");
  for (auto& p : match.players) p.kind = -1;
  for (unsigned i = 0; i < 2; ++i) {
    auto& p = match.players[i];
    p.kind = integer(in, 0, 25); // retail CharacterKind roster
    p.costume = integer(in, 0, 3); // common safe subset across the roster
    p.cpu = integer(in, 0, 1);
    p.cpu_level = integer(in, 1, 9);
  }
  in >> std::ws;
  if (!in.eof()) {
    std::string tag;
    in >> tag;
    if (tag != "ports") throw std::runtime_error("control: expected optional ports");
    const int first = integer(in, 0, 3), second = integer(in, 0, 3);
    if (first == second) throw std::runtime_error("control: player ports must differ");
    const auto a = match.players[0], b = match.players[1];
    for (auto& p : match.players) p.kind = -1;
    match.players[first] = a; match.players[second] = b;
  }
  end(in);
  return match;
}
void parse_stock_rules(const std::string& value, int32_t& seconds, int32_t& stocks) {
  std::istringstream in(value);
  const int parsed_seconds = integer(in, 60, 600), parsed_stocks = integer(in, 1, 4);
  end(in);
  if (parsed_seconds % 60) throw std::runtime_error("control: stock timer requires whole minutes");
  seconds = parsed_seconds; stocks = parsed_stocks;
}
void write_observation(std::ostream& out, const MuStatePod& state, const Counters& c) {
  out << c.retraces << ',' << state.scene_major << ',' << state.scene << ','
      << state.match_frame << ',' << state.rng;
  for (const auto& f : state.player)
    out << ',' << f.present << ',' << f.stocks << ',' << f.action << ','
        << f.anim_frame << ',' << f.pos_x << ',' << f.pos_y << ',' << f.pos_z << ','
        << f.vel_x << ',' << f.vel_y << ',' << f.vel_z << ',' << f.percent << ',' << f.facing;
  out << '\n';
}
RetraceControl::RetraceControl(int fd, uint32_t limit, std::function<void()> flush,
                               std::function<std::vector<uint8_t>()> replay,
                               std::function<RecordingStatus()> recording_status,
                               bool include_preroll)
    : replay_(std::move(replay)), recording_status_(std::move(recording_status)),
      flush_(std::move(flush)), fd_(fd), boot_limit_(limit), include_preroll_(include_preroll) {
  if (fd < 3 || !limit) throw std::runtime_error("control: invalid socket or boot limit");
}
RetraceControl::~RetraceControl() { close(fd_); }
void RetraceControl::send(const std::string& text) {
  size_t offset = 0;
  while (offset < text.size()) {
    const auto n = ::send(fd_, text.data() + offset, text.size() - offset, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) throw std::runtime_error("control: socket write failed");
    offset += static_cast<size_t>(n);
  }
}
bool RetraceControl::receive(std::string& line) {
  line.clear();
  while (line.size() < 4096) {
    char byte;
    const auto n = recv(fd_, &byte, 1, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n == 0 && line.empty()) return false;
    if (n <= 0) throw std::runtime_error("control: socket read failed/truncated command");
    if (byte == '\n') return true;
    line += byte;
  }
  throw std::runtime_error("control: command exceeds 4096 bytes");
}
bool RetraceControl::boundary(const MuStatePod& state, const Counters& c) {
  if (!ready_) {
    // VS major/minor and live fighters, not a stale menu match-frame counter.
    if (!live_two_player_match(state, include_preroll_)) {
      if (c.retraces >= boot_limit_) throw std::runtime_error("control: no match before boot retrace limit");
      return true;
    }
    ready_ = true;
    send("MUCONTROL 2\n");
  }
  std::ostringstream out;
  out << "STATE ";
  write_observation(out, state, c);
  send(out.str());
  std::string line;
  while (receive(line)) {
    if (line == "quit") return false;
    if (line == "observe") { send(out.str()); continue; }
    if (line == "record-status") {
      RecordingStatus status{};
      try {
        if (!recording_status_) throw std::runtime_error("no recording status provider");
        status = recording_status_();
      } catch (const std::exception& error) {
        send(std::string("UNSUPPORTED ") + error.what() + "\n"); continue;
      }
      send("RECORDING " + std::to_string(status.last_frame) + " " + (status.ended ? "1\n" : "0\n"));
      continue;
    }
    if (line == "record") {
      std::vector<uint8_t> bytes;
      try {
        if (!replay_) throw std::runtime_error("no replay event sink");
        bytes = replay_();
      } catch (const std::exception& error) {
        send(std::string("UNSUPPORTED ") + error.what() + "\n");
        continue;
      }
      send("REPLAY " + std::to_string(bytes.size()) + "\n");
      constexpr char hex[] = "0123456789abcdef";
      for (size_t offset = 0; offset < bytes.size(); offset += 1024) {
        std::string chunk;
        for (size_t i = offset; i < bytes.size() && i < offset + 1024; ++i) {
          chunk += hex[bytes[i] >> 4]; chunk += hex[bytes[i] & 15];
        }
        send(chunk + "\n");
      }
      send("RECORDED\n");
      continue;
    }
    if (line == "capture") {
      if (!live_two_player_match(state, include_preroll_)) {
        send("UNSUPPORTED checkpoint requires a live two-player VS match\n"); continue;
      }
      try {
        if (flush_) flush_();
        const auto generation = checkpoint_.capture();
        send(generation ? "RESTORED\n" : "CAPTURED\n");
        send(out.str());
      } catch (const std::exception& error) {
        send(std::string("UNSUPPORTED ") + error.what() + "\n");
      }
      continue;
    }
    if (line == "restore") {
      if (!checkpoint_.active()) { send("UNSUPPORTED no process checkpoint\n"); continue; }
      if (flush_) flush_();
      checkpoint_.restore();
    }
    std::istringstream in(line);
    std::string command;
    in >> command;
    if (command != "step") throw std::runtime_error("control: unknown command");
    std::array<MuPadStatus, 4> next{};
    for (auto& p : next) {
      p.button = integer(in, 0, 65535);
      p.stick_x = integer(in, -128, 127); p.stick_y = integer(in, -128, 127);
      p.sub_x = integer(in, -128, 127); p.sub_y = integer(in, -128, 127);
      p.trigger_l = integer(in, 0, 255); p.trigger_r = integer(in, 0, 255);
      p.analog_a = integer(in, 0, 255); p.analog_b = integer(in, 0, 255);
      p.err = integer(in, -1, 0);
    }
    end(in); // validate all ports before changing any input
    pads_ = next;
    return true;
  }
  return false;
}
}
