// timer.hpp — RAII stage timer used for profiling (sequential fraction f).
#pragma once

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

namespace citeverify {

// RAII = "Resource Acquisition Is Initialization": the constructor starts the
// clock, the destructor stops it. Because destructors run automatically when
// a scope ends (even on early return or exception), we can never forget to
// stop a timer.
//
// steady_clock is monotonic: it never jumps backwards when the system clock
// is adjusted (NTP, daylight saving), so it is the right clock for durations.
class ScopedTimer {
public:
    // `out_ms` is optional: if given, the elapsed time is also stored there so
    // the program can compute the sequential fraction at the end.
    explicit ScopedTimer(std::string name, double* out_ms = nullptr, bool print = true)
        : name_(std::move(name)), out_ms_(out_ms), print_(print),
          start_(std::chrono::steady_clock::now()) {}

    // Timers must not be copied: two copies would both report the same stage.
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    ~ScopedTimer() {
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start_).count();
        if (out_ms_) *out_ms_ = ms;
        if (print_) std::cerr << "[timer] " << name_ << ": " << ms << " ms\n";
    }

private:
    std::string name_;
    double* out_ms_;
    bool print_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace citeverify
