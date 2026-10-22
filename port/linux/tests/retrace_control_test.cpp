// Asset-independent protocol test. Synthetic counters are NOT gameplay evidence.
#include "retrace_control.h"
#include <future>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
using namespace linux_host;
using namespace std::chrono_literals;
void require(bool good) { if (!good) throw std::runtime_error("control test failed"); }
void send_line(int fd, const std::string& line) {
  require(send(fd, line.data(), line.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(line.size()));
}
std::string read_line(int fd) {
  std::string line;
  char c;
  while (recv(fd, &c, 1, 0) == 1) { if (c == '\n') return line; line += c; }
  throw std::runtime_error("unexpected EOF");
}
MuStatePod match() {
  MuStatePod s{}; s.scene_major = 2; s.scene = 2; s.match_frame = 1;
  s.player[0].present = s.player[1].present = 1;
  s.player[0].pos_x = 0xbf800000;
  return s;
}
int main() {
  try {
    auto fixed = parse_fixed_match("31 2 0 0 9 20 3 1 4");
    require(fixed.stage == 31 && fixed.players[0].kind == 2 && fixed.players[1].kind == 20);
    require(fixed.players[1].cpu == 1 && fixed.players[1].costume == 3 && fixed.players[5].kind == -1);
    for (const auto* invalid : {"", "30 2 0 0 9 20 0 0 9", "31 26 0 0 9 20 0 0 9",
         "31 2 4 0 9 20 0 0 9", "31 2 0 0 0 20 0 0 9", "31 2 0 2 9 20 0 0 9",
         "31 2 0 0 9 20 0 0 9 extra", "31 2 0 0 9 20 0 0"}) {
      bool rejected = false;
      try { parse_fixed_match(invalid); } catch (const std::exception&) { rejected = true; }
      require(rejected);
    }
    int fd[2]; require(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    std::atomic<bool> ended{false};
    RetraceControl control(fd[1], 10, {}, [] { return std::vector<uint8_t>(1500, 0xab); },
                           [&] { return RecordingStatus{9, ended.load()}; });
    Counters c{}; c.retraces = 1; c.ticks = 1234;
    MuStatePod menu{}; menu.match_frame = 99;
    require(control.boundary(menu, c) && !control.ready());
    auto state = match();
    auto worker = std::async(std::launch::async, [&] { return control.boundary(state, c); });
    require(read_line(fd[0]) == "MUCONTROL 2");
    const auto initial = read_line(fd[0]);
    require(initial.find("3212836864") != std::string::npos);
    require(worker.wait_for(20ms) == std::future_status::timeout);
    send_line(fd[0], "observe\n"); require(read_line(fd[0]) == initial);
    send_line(fd[0], "record-status\n"); require(read_line(fd[0]) == "RECORDING 9 0");
    ended = true;
    send_line(fd[0], "record-status\n"); require(read_line(fd[0]) == "RECORDING 9 1");
    send_line(fd[0], "record\n");
    require(read_line(fd[0]) == "REPLAY 1500");
    std::string hex;
    for (int i = 0; i < 1024; ++i) hex += "ab";
    require(read_line(fd[0]) == hex);
    require(read_line(fd[0]) == hex.substr(0, 952));
    require(read_line(fd[0]) == "RECORDED");
    send_line(fd[0], "observe\n"); require(read_line(fd[0]) == initial);
    send_line(fd[0], "capture\n");
    require(read_line(fd[0]).rfind("UNSUPPORTED checkpoint requires exactly one native thread",0)==0);
    send_line(fd[0], "restore\n");
    require(read_line(fd[0])=="UNSUPPORTED no process checkpoint");
    require(worker.wait_for(20ms) == std::future_status::timeout);
    require(c.retraces == 1 && c.ticks == 1234); // callback never mutates host clocks
    send_line(fd[0], "step 256 -128 127 -1 1 255 0 128 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 -1 0 0 0 0 0 0 0 0 0 -1\n");
    require(worker.get());
    require(control.pads()[0].button == 256 && control.pads()[0].stick_x == -128);
    require(control.pads()[0].trigger_l == 255 && control.pads()[2].err == -1);
    ++c.retraces; ++state.match_frame;
    worker = std::async(std::launch::async, [&] { return control.boundary(state, c); });
    require(read_line(fd[0]).rfind("STATE 2,2,2,2,", 0) == 0);
    send_line(fd[0], "quit\n"); require(!worker.get()); close(fd[0]);

    // Invalid input must abort without partially applying any port's input.
    for (const auto& command : {std::string("step 999999\n"), std::string("step\n"),
                               std::string("unknown\n"), std::string(4097, 'x') + "\n", std::string("step 0")}) {
      int pair[2]; require(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
      RetraceControl bad(pair[1], 1);
      auto task = std::async(std::launch::async, [&] {
        try { bad.boundary(state, c); return false; } catch (const std::exception&) { return true; }
      });
      read_line(pair[0]); read_line(pair[0]); send_line(pair[0], command);
      shutdown(pair[0], SHUT_WR);
      require(task.get()); require(bad.pads()[0].button == 0); close(pair[0]);
    }
    int pair[2]; require(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    RetraceControl eof(pair[1], 1);
    auto task = std::async(std::launch::async, [&] { return eof.boundary(state, c); });
    read_line(pair[0]); read_line(pair[0]); close(pair[0]); require(!task.get());
    bool limited = false;
    try { RetraceControl timeout(99999, 1); timeout.boundary(menu, c); }
    catch (const std::exception&) { limited = true; }
    require(limited);
    std::cout << "native retrace control protocol: PASS (synthetic state)\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
