// Test transport only: no game, ISO, timing or physics is simulated here.
#include "retrace_control.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
int main(int argc, char** argv) {
  int fd = -1;
  bool preroll = false;
  std::string digest_path;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--control-fd") fd = std::stoi(argv[++i]);
    else if (std::string(argv[i]) == "--state-digest") digest_path = argv[++i];
    else if (std::string(argv[i]) == "--control-preroll") preroll = true;
  }
  try {
    std::ofstream digest(digest_path);
    linux_host::RetraceControl control(fd, 10, [&] { digest.flush(); }, {}, {}, preroll);
    linux_host::Counters counters{};
    MuStatePod state{};
    state.scene_major = state.scene = 2;
    state.player[0].present = state.player[1].present = 1;
    while (true) {
      ++counters.retraces;
      // Explicit synthetic zero-counter boundaries for transport tests only.
      if (!preroll || counters.retraces > 3) ++state.match_frame;
      state.player[0].pos_x += control.pads()[0].button;
      linux_host::write_observation(digest, state, counters);
      if (!control.boundary(state, counters)) break;
    }
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
