// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Common {

/// The state shared between the console-mode engine (in the main window, ticking at
/// 250ms) and its settings UI (the applets tab): which pads are live, which one is
/// the handheld's own, and the "identify" handshake. The UI reads and declares; the
/// engine observes and obeys.
struct ControllerConsoleState {
    std::mutex mutex;
    /// Live pads as (display name, identity) in engine order
    std::vector<std::pair<std::string, std::string>> pads;
    /// Set by the Identify button: capture the next pad that has a button pressed
    bool identify_armed = false;
    std::chrono::steady_clock::time_point identify_armed_at{};
};

[[nodiscard]] inline ControllerConsoleState& GetControllerConsoleState() {
    static ControllerConsoleState instance;
    return instance;
}

} // namespace Common
