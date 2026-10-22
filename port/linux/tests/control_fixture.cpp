// Test transport only: no game, ISO, timing or physics is simulated here.
#include "retrace_control.h"
#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  int fd = -1;
  for (int i = 1; i + 1 < argc; ++i)
    if (std::string(argv[i]) == "--control-fd") fd = std::stoi(argv[++i]);
  try {
    linux_host::RetraceControl control(fd, 10);
    linux_host::Counters counters{};
    MuStatePod state{};
    state.scene_major = state.scene = 2;
    state.player[0].present = state.player[1].present = 1;
    while (true) {
      ++counters.retraces;
      ++state.match_frame;
      state.player[0].pos_x += control.pads()[0].button;
      if (!control.boundary(state, counters)) break;
    }
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
