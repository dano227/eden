// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.features.input

/**
 * Android-side representation of the controller assignment modes introduced by desktop PR #4583.
 *
 * This class deliberately only plans session ports. It never edits PlayerInput, profiles, or
 * persisted button/analog mappings.
 */
enum class ControllerAssignmentMode {
    MANUAL,
    REAL_HARDWARE,
    MODERN_CONSOLE;

    companion object {
        fun fromPreference(value: String?): ControllerAssignmentMode =
            values().firstOrNull { it.name == value } ?: MANUAL
    }
}

/** A physical Android input device and the session-only facts needed to assign it. */
data class ControllerCandidate(
    val androidControllerNumber: Int,
    val guid: String,
    val displayName: String,
    val isBuiltInHandheld: Boolean
)

/** The result is intentionally session-only; playerIndex is zero-based. */
data class ControllerPlayerAssignment(
    val controller: ControllerCandidate,
    val playerIndex: Int
)

object ControllerAssignment {
    /**
     * Computes player slots without changing saved controller configuration.
     *
     * Manual keeps Android's existing port order. Modern Console puts external pads first and
     * optionally reserves the final slot for the built-in controls. Real Hardware returns no
     * Android assignments because that mode delegates pad selection to the console HID applet.
     */
    fun assign(
        controllers: List<ControllerCandidate>,
        mode: ControllerAssignmentMode,
        handheldEnabled: Boolean = true
    ): List<ControllerPlayerAssignment> {
        if (mode == ControllerAssignmentMode.REAL_HARDWARE) return emptyList()

        val ordered = when (mode) {
            ControllerAssignmentMode.MANUAL ->
                controllers.sortedBy { it.androidControllerNumber }

            ControllerAssignmentMode.MODERN_CONSOLE -> {
                val external = controllers
                    .filterNot { it.isBuiltInHandheld }
                    .sortedBy { it.androidControllerNumber }
                val handheld = controllers
                    .filter { it.isBuiltInHandheld }
                    .sortedBy { it.androidControllerNumber }
                external + if (handheldEnabled) handheld else emptyList()
            }

            ControllerAssignmentMode.REAL_HARDWARE -> emptyList()
        }

        return ordered.take(8).mapIndexed { playerIndex, controller ->
            ControllerPlayerAssignment(controller, playerIndex)
        }
    }
}
