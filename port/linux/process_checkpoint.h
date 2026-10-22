// Linux-only, single-threaded process checkpoint. No portable state serialization.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace linux_host {
class ProcessCheckpoint {
 public:
  ~ProcessCheckpoint();
  // Parent retains the stack here and supervises one running branch. Returns in
  // the branch only: 0 on capture, >0 after each restore from this same parent.
  unsigned capture();
  [[noreturn]] void restore();
  bool active() const { return restore_fd_ >= 0; }
 private:
  int restore_fd_ = -1;
};
}
