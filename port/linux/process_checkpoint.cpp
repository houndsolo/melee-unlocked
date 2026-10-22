// SPDX-License-Identifier: GPL-3.0-or-later
#include "process_checkpoint.h"
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
namespace linux_host {
ProcessCheckpoint::~ProcessCheckpoint() { if (restore_fd_ >= 0) close(restore_fd_); }
unsigned ProcessCheckpoint::capture() {
  if (active()) throw std::runtime_error("one checkpoint per session; close/start to capture another");
  unsigned threads = 0;
  for (const auto& ignored : std::filesystem::directory_iterator("/proc/self/task")) { (void)ignored; ++threads; }
  if (threads != 1) throw std::runtime_error("checkpoint requires exactly one native thread");
  // Neither stdio buffers nor C++ output buffers may be duplicated into branches.
  // The caller flushes its C++ streams before entering this function.
  std::fflush(nullptr);
  const pid_t owner = getpid();
  for (unsigned generation = 0; ; ++generation) {
    int marker[2];
    if (pipe(marker) != 0) throw std::runtime_error("checkpoint pipe failed");
    const pid_t child = fork();
    if (child < 0) { close(marker[0]); close(marker[1]); throw std::runtime_error("checkpoint fork failed"); }
    if (child == 0) {
      close(marker[0]);
      // Covers owner death, including Python's forced cleanup; check the race
      // where the owner dies between fork and installing the death signal.
      if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != owner) _exit(1);
      restore_fd_ = marker[1];
      return generation;
    }
    close(marker[1]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    char request = 0;
    ssize_t bytes;
    do { bytes = read(marker[0], &request, 1); } while (bytes < 0 && errno == EINTR);
    close(marker[0]);
    // An exit code alone is never interpreted as a restore request.
    if (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 42 && bytes == 1 && request == 'R')
      continue;
    // EOF/quit/failure ends the frozen parent too, without running game cleanup
    // twice or flushing pre-capture output a second time.
    _exit(waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : 1);
  }
}
[[noreturn]] void ProcessCheckpoint::restore() {
  if (!active()) throw std::runtime_error("no process checkpoint");
  std::fflush(nullptr);
  const char request = 'R';
  ssize_t bytes;
  do { bytes = write(restore_fd_, &request, 1); } while (bytes < 0 && errno == EINTR);
  _exit(bytes == 1 ? 42 : 1);
}
}
