// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Dependency-free test support for the core tests: a failure collector and a
// seeded RNG. The same test functions run under plain CMake + ctest
// (core/test/run-core-tests.cc, no simulator) and under ns-3's test.py
// (test/swarm-core-test-suite.cc), so a local check needs no ns-3 tree.
// The fake-port idea is from swarm-routing/AntHocNet core/tests/test_support.h
// @ 78c4640 (MIT OR GPL-2.0-only).
#ifndef SWARM_CORE_TEST_CHECK_H
#define SWARM_CORE_TEST_CHECK_H

#include "../swarm-ports.h"

#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace swarm::core::test {

/// Collects failed checks, each with its file and line.
class Checker {
public:
    void check(bool ok, std::string_view what,
               std::source_location where = std::source_location::current()) {
        if (ok) return;
        failures_.push_back(std::string(where.file_name()) + ":" + std::to_string(where.line()) +
                            ": " + std::string(what));
    }
    const std::vector<std::string>& failures() const { return failures_; }

private:
    std::vector<std::string> failures_;
};

/// SplitMix64: a tiny, fully specified generator, so a seed gives the same
/// sequence on every compiler and platform (the tests compare sequences).
class SeededRng : public IRng {
public:
    explicit SeededRng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() override {
        ++draws_;
        return static_cast<double>(next() >> 11) * 0x1.0p-53;
    }
    int uniformInt(int n) override {
        ++draws_;
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }
    /// How many values were drawn through the IRng port.
    int draws() const { return draws_; }

private:
    std::uint64_t state_;
    int draws_ = 0;
};

/// An RNG that always returns the same value: for boundary cases.
class FixedRng : public IRng {
public:
    explicit FixedRng(double u) : u_(u) {}
    double uniform() override { return u_; }
    int uniformInt(int /*n*/) override { return 0; }

private:
    double u_;
};

}  // namespace swarm::core::test

#endif  // SWARM_CORE_TEST_CHECK_H
