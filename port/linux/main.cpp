// Native Linux entry point for Melee Unlocked's source game.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_services.h"
#include "retrace_control.h"
#include <memory>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef MELEE_LINUX_GRAPHICS
#include "renderer_gl.h"
#include "gx_host_compat.h"
#include "renderer_presenter.h"
#include <SDL.h>
#endif

namespace {
#ifdef MELEE_LINUX_GRAPHICS
std::filesystem::path shader_cache() {
  if (const char* cache = std::getenv("XDG_CACHE_HOME"); cache && *cache && std::filesystem::path(cache).is_absolute())
    return std::filesystem::path(cache) / "melee-unlocked/shaders";
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".cache/melee-unlocked/shaders";
  return std::filesystem::temp_directory_path() / "melee-unlocked-shaders";
}
struct Presentation {
  SDL_Window* window = nullptr;
  std::unique_ptr<gx::Backend> backend;
  ~Presentation() { close(); }
  void close() {
    gx::init(nullptr);
    backend.reset();
    if (window) SDL_DestroyWindow(window);
    window = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  void open_window(bool hidden) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Melee Unlocked " MELEE_PORT_VERSION " - Linux",
       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!window) throw std::runtime_error(SDL_GetError());
  }
  void open(bool hidden, bool threaded = false, double fps = 120) {
    open_window(hidden);
    backend = threaded ? linux_renderer::create_threaded(window, shader_cache(), 1, fps, false)
                       : linux_renderer::create(window, shader_cache(), 1, !hidden);
    if (!backend) throw std::runtime_error("OpenGL renderer initialization failed");
    linux_renderer::connect_host();
    gx::init(backend.get());
  }
};
void fifo(const uint8_t* bytes, uint32_t size, void*) { gx::write_fifo_bytes(bytes, size); }
#endif
uint64_t number(const char* value, const char* name) {
  if (!value || !*value || *value == '-') throw std::runtime_error(std::string("invalid ") + name);
  char* end = nullptr;
  errno = 0;
  const auto result = std::strtoull(value, &end, 0);
  if (errno || *end) throw std::runtime_error(std::string("invalid ") + name);
  return result;
}
void usage() {
  std::puts("Melee Unlocked native Linux\n"
            "  melee_linux --iso <Melee NTSC 1.02.iso> [--frames N] [--headless] [--fast]\n"
            "              [--game-library path] [--log-file path] [--state-digest path]\n"
            "              [--fps 60..1000] [--card-dir path] [--no-gc-adapter]\n"
            "              [--input-script path] [--capture frame.ppm] [--vanilla-game]\n"
            "              [--rng-seed N] [--time-base N] [--trace-state]\n"
            "              [--match 'stage p1_kind costume cpu level p2_kind costume cpu level']\n"
            "              [--control-fd N --boot-limit N] (headless/fast; one VI per step)\n"
            "  melee_linux --memory-self-test | --version\n"
            "\nRequires a retail NTSC 1.02 ISO. Default display: 120 FPS; gameplay: 60 Hz.\n"
            "Keyboard: arrows = stick, Z/X/C/V = A/B/X/Y, Enter = Start,\n"
            "          Q/E = L/R, S = Z, I/J/K/L = C-stick.\n");
}
struct Observer {
  std::ofstream digest;
  std::unique_ptr<linux_host::RetraceControl> control;
};
void observe(const MuStatePod& state, const linux_host::Counters& counters, void* user) {
  auto& observer = *static_cast<Observer*>(user);
  if (observer.digest.is_open()) linux_host::write_observation(observer.digest, state, counters);
  if (observer.control) {
    if (!observer.control->boundary(state, counters)) linux_host::request_stop();
    else if (observer.control->ready())
      for (unsigned port = 0; port < 4; ++port)
        linux_host::set_pad_override(port, observer.control->pads()[port]);
  }
}
// The Windows build's check (port/app/main.cpp): same wording, same exit code.
bool cpu_has_baseline() {
  __builtin_cpu_init();
  const std::string baseline = MELEE_LINUX_CPU_BASELINE;
  if (baseline == "AVX2") return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
  if (baseline == "AVX") return __builtin_cpu_supports("avx");
  return true;
}
}

int main(int argc, char** argv) {
  if (!cpu_has_baseline()) {
    std::fprintf(stderr, "Melee Unlocked needs a CPU with %s%s. This CPU does not support it.\n",
                 MELEE_LINUX_CPU_BASELINE, std::strcmp(MELEE_LINUX_CPU_BASELINE, "AVX2") ? "" :
                 " (Intel Haswell 2013 or newer, AMD Ryzen or newer)");
    return 3;
  }
  try {
    linux_host::Config config;
    std::string digest_path, graphics_capture, capture_path;
    double display_fps = 120;
    bool presenter_test = false;
    int control_fd = -1;
    uint32_t boot_limit = 6000;
    std::error_code path_error;
    const auto binary = std::filesystem::read_symlink("/proc/self/exe", path_error);
    if (!path_error) config.game_path = (binary.parent_path() / "melee_game.so").string();
    if (!std::getenv("MELEE_DSP_COEF") && !path_error) {
      const auto coefficients = binary.parent_path() / "Sys/GC/dsp_coef.bin";
      if (std::filesystem::is_regular_file(coefficients)) setenv("MELEE_DSP_COEF", coefficients.c_str(), 0);
    }
    for (int index = 1; index < argc; ++index) {
      const std::string arg(argv[index]);
      auto next = [&]() -> const char* {
        if (++index >= argc) throw std::runtime_error("missing value after " + arg);
        return argv[index];
      };
      if (arg == "--help" || arg == "-h") { usage(); return 0; }
      if (arg == "--version") { std::puts(MELEE_PORT_VERSION); return 0; }
      if (arg == "--host-self-test") {
        std::string error;
        if (!linux_host::service_self_test(next(), &error)) throw std::runtime_error(error);
        std::puts("native Linux machine services: PASS");
        return 0;
      }
      if (arg == "--memory-self-test") {
        std::string error;
        if (!linux_host::reserve_memory(&error)) throw std::runtime_error(error);
        auto* memory = reinterpret_cast<volatile unsigned char*>(linux_host::mem1_base);
        memory[0] = 0x5a;
        memory[linux_host::mem1_bytes - 1] = 0xa5;
        if (memory[0] != 0x5a || memory[linux_host::mem1_bytes - 1] != 0xa5)
          throw std::runtime_error("fixed RAM mapping self-test failed");
        linux_host::shutdown();
        std::puts("native Linux fixed RAM mapping: PASS");
        return 0;
      }
      if (arg == "--iso") config.iso_path = next();
      else if (arg == "--graphics-self-test") graphics_capture = next();
      else if (arg == "--presenter-self-test") presenter_test = true;
      else if (arg == "--capture") capture_path = next();
      else if (arg == "--fps") {
        display_fps = static_cast<double>(number(next(), "display fps"));
        if (display_fps < 60 || display_fps > 1000) throw std::runtime_error("--fps must be 60..1000");
      }
      else if (arg == "--no-gc-adapter") config.no_gc_adapter = true;
      else if (arg == "--vanilla-game") config.vanilla_game = true;
      else if (arg == "--game-library") config.game_path = next();
      else if (arg == "--log-file") config.log_path = next();
      else if (arg == "--card-dir") config.card_dir = next();
      else if (arg == "--input-script") config.input_script = next();
      else if (arg == "--state-digest") digest_path = next();
      else if (arg == "--match") {
        config.match = linux_host::parse_fixed_match(next()); config.has_match = true;
      }
      else if (arg == "--control-fd") {
        const auto fd = number(next(), "control fd");
        if (fd < 3 || fd > INT32_MAX) throw std::runtime_error("invalid control fd");
        control_fd = static_cast<int>(fd);
      }
      else if (arg == "--boot-limit") {
        const auto limit = number(next(), "boot limit");
        if (!limit || limit > UINT32_MAX) throw std::runtime_error("invalid boot limit");
        boot_limit = static_cast<uint32_t>(limit);
      }
      else if (arg == "--frames") {
        const auto frames = number(next(), "frame count");
        if (frames > UINT32_MAX) throw std::runtime_error("frame count exceeds uint32");
        config.max_frames = static_cast<uint32_t>(frames);
      }
      else if (arg == "--headless") config.headless = true;
      else if (arg == "--fast") config.pace = false;
      else if (arg == "--trace-state") config.trace_state = true;
      else if (arg == "--rng-seed") {
        auto value = number(next(), "rng seed");
        if (value > UINT32_MAX) throw std::runtime_error("rng seed exceeds uint32");
        config.has_rng_seed = true; config.rng_seed = static_cast<uint32_t>(value);
      }
      else if (arg == "--time-base") {
        config.has_time_base = true; config.time_base = number(next(), "time base");
      }
      else throw std::runtime_error("unknown option: " + arg);
    }
#ifdef MELEE_LINUX_GRAPHICS
    if (presenter_test) {
      Presentation presentation;
      presentation.open_window(true);
      if (!linux_renderer::presenter_self_test(presentation.window, shader_cache() / "presenter-test"))
        throw std::runtime_error("native unlocked presenter self-test failed");
      std::puts("native Linux unlocked presenter: PASS");
      return 0;
    }
    if (!graphics_capture.empty()) {
      std::string error;
      if (!linux_host::reserve_memory(&error)) throw std::runtime_error(error);
      Presentation presentation;
      presentation.open(true);
      if (!linux_renderer::smoke_test(*presentation.backend, graphics_capture))
        throw std::runtime_error("GX shader/render self-test failed");
      std::puts("native Linux GX renderer: PASS");
      return 0;
    }
#endif
    if (config.iso_path.empty()) { usage(); return 2; }
#ifndef MELEE_LINUX_GRAPHICS
    if (!config.headless) throw std::runtime_error("this build has no graphics; rebuild with MELEE_LINUX_GRAPHICS=ON");
#endif
    if (control_fd >= 0 && (!config.headless || config.pace || config.max_frames || !config.has_match))
      throw std::runtime_error("control requires --headless --fast --match, without --frames");
    std::string error;
    if (!linux_host::initialize(config, &error)) throw std::runtime_error(error);
#ifdef MELEE_LINUX_GRAPHICS
    Presentation presentation;
    if (!config.headless) {
      presentation.open(false, true, display_fps);
      linux_host::set_video_sinks(fifo, nullptr, nullptr);
    }
#endif
    Observer observer;
    if (!digest_path.empty()) {
      observer.digest.open(digest_path);
      if (!observer.digest) throw std::runtime_error("cannot open state digest output");
    }
    if (control_fd >= 0)
      observer.control = std::make_unique<linux_host::RetraceControl>(control_fd, boot_limit);
    if (observer.control || observer.digest.is_open()) linux_host::set_frame_observer(observe, &observer);
    const int result = linux_host::run();
#ifdef MELEE_LINUX_GRAPHICS
    if (!capture_path.empty() && presentation.backend)
      linux_renderer::capture_efb(*presentation.backend, capture_path);
    if (presentation.backend) {
      const auto rendered = linux_renderer::presenter_stats(*presentation.backend);
      std::printf("Linux display: %llu source frames, %llu display frames, %llu authored subframes, %llu posed draws\n",
        static_cast<unsigned long long>(rendered.simulation), static_cast<unsigned long long>(rendered.presentation),
        static_cast<unsigned long long>(rendered.subframes), static_cast<unsigned long long>(rendered.posed));
    }
    // Drain the render queue before the game image and its static data are unmapped.
    presentation.close();
#endif
    const auto stats = linux_host::counters();
    std::printf("native Linux: %u retraces, %llu disc reads, %llu FIFO bytes, %llu audio blocks\n",
                stats.retraces, static_cast<unsigned long long>(stats.disc_reads),
                static_cast<unsigned long long>(stats.fifo_bytes),
                static_cast<unsigned long long>(stats.audio_blocks));
    linux_host::shutdown();
    return result;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Melee Unlocked Linux: %s\n", error.what());
    linux_host::shutdown();
    return 1;
  }
}
