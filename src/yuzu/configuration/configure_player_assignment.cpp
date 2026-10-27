// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "common/controller_console_state.h"
#include "common/settings.h"
#include "yuzu/configuration/configure_player_assignment.h"

namespace {

bool LaunchedThroughSteam() {
    return std::getenv("SteamGameId") != nullptr || std::getenv("SteamAppId") != nullptr;
}

bool IsAutomatic(Settings::AppletMode mode) {
    return mode == Settings::AppletMode::ConsoleMode ||
           mode == Settings::AppletMode::HandheldConsoleMode;
}

} // Anonymous namespace

ConfigurePlayerAssignment::ConfigurePlayerAssignment(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // How players are assigned. Underneath this is the controller applet's mode: the
    // automatic modes own the players and keep the applet from opening.
    auto* mode_box = new QGroupBox(tr("Player assignment"), this);
    auto* mode_layout = new QVBoxLayout(mode_box);
    auto* mode_row = new QHBoxLayout();
    mode_row->addWidget(new QLabel(tr("Assign players:"), mode_box));
    mode = new QComboBox(mode_box);
    mode->addItem(tr("Automatically, one per controller"),
                  static_cast<int>(Settings::AppletMode::ConsoleMode));
    mode->addItem(tr("Automatically, handheld"),
                  static_cast<int>(Settings::AppletMode::HandheldConsoleMode));
    mode->addItem(tr("Manually, with Eden's controller window"),
                  static_cast<int>(Settings::AppletMode::HLE));
    mode->addItem(tr("Manually, with the console's controller window"),
                  static_cast<int>(Settings::AppletMode::LLE));
    mode->addItem(tr("Manually, no controller window"),
                  static_cast<int>(Settings::AppletMode::Disabled));
    mode_row->addWidget(mode, 1);
    mode_layout->addLayout(mode_row);
    mode_help = new QLabel(mode_box);
    mode_help->setWordWrap(true);
    mode_layout->addWidget(mode_help);

    // The handheld's own built-in controls: only meaningful for the handheld mode
    builtin_row = new QWidget(mode_box);
    auto* builtin_layout = new QVBoxLayout(builtin_row);
    builtin_layout->setContentsMargins(0, 6, 0, 0);
    auto* picker_row = new QHBoxLayout();
    picker_row->addWidget(new QLabel(tr("Built-in controls:"), builtin_row));
    builtin_picker = new QComboBox(builtin_row);
    builtin_picker->addItem(tr("Detect automatically"), QString{});
    picker_row->addWidget(builtin_picker, 1);
    builtin_identify = new QPushButton(tr("Identify..."), builtin_row);
    builtin_identify->setToolTip(
        tr("Press a button on the handheld's own controls to pick them."));
    picker_row->addWidget(builtin_identify);
    builtin_layout->addLayout(picker_row);
    builtin_status = new QLabel(builtin_row);
    builtin_status->setWordWrap(true);
    builtin_layout->addWidget(builtin_status);
    mode_layout->addWidget(builtin_row);
    layout->addWidget(mode_box);

    // The players right now, as the engine assigned them
    auto* now_box = new QGroupBox(tr("Players now"), this);
    auto* now_layout = new QVBoxLayout(now_box);
    players_box = new QWidget(now_box);
    players_grid = new QGridLayout(players_box);
    players_grid->setContentsMargins(0, 0, 0, 0);
    players_grid->setColumnStretch(1, 1);
    now_layout->addWidget(players_box);
    players_note = new QLabel(now_box);
    players_note->setWordWrap(true);
    now_layout->addWidget(players_note);
    layout->addWidget(now_box);
    layout->addStretch(1);

    connect(mode, &QComboBox::currentIndexChanged, this, [this] { UpdateModeUi(); });
    connect(builtin_picker, &QComboBox::currentIndexChanged, this,
            &ConfigurePlayerAssignment::OnBuiltinPicked);
    connect(builtin_identify, &QPushButton::clicked, this,
            &ConfigurePlayerAssignment::OnIdentifyPressed);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ConfigurePlayerAssignment::Refresh);
    timer->start(250);

    LoadConfiguration();
    Refresh();
}

ConfigurePlayerAssignment::~ConfigurePlayerAssignment() = default;

void ConfigurePlayerAssignment::LoadConfiguration() {
    const int current = static_cast<int>(Settings::values.controller_applet_mode.GetValue());
    const int index = mode->findData(current);
    mode->setCurrentIndex(index >= 0 ? index : mode->findData(
                                                   static_cast<int>(Settings::AppletMode::HLE)));
    UpdateModeUi();
}

void ConfigurePlayerAssignment::ApplyConfiguration() {
    Settings::values.controller_applet_mode.SetValue(
        static_cast<Settings::AppletMode>(mode->currentData().toInt()));
}

void ConfigurePlayerAssignment::UpdateModeUi() {
    const auto selected = static_cast<Settings::AppletMode>(mode->currentData().toInt());
    switch (selected) {
    case Settings::AppletMode::ConsoleMode:
        mode_help->setText(tr("Like a console: each controller that connects becomes the next "
                              "player, and keeps its player if it disconnects and returns."));
        break;
    case Settings::AppletMode::HandheldConsoleMode:
        mode_help->setText(
            tr("Like a handheld console: the built-in controls are player 1 while no other "
               "controller is connected. Connected controllers take the players from 1."));
        break;
    case Settings::AppletMode::HLE:
        mode_help->setText(tr("Players come from each player's tab. Games that ask for "
                              "controllers open Eden's controller window."));
        break;
    case Settings::AppletMode::LLE:
        mode_help->setText(tr("Players come from each player's tab. Games that ask for "
                              "controllers open the console's own controller window."));
        break;
    default:
        mode_help->setText(tr("Players come from each player's tab. Games that ask for "
                              "controllers get the configured players without a window."));
        break;
    }
    builtin_row->setVisible(selected == Settings::AppletMode::HandheldConsoleMode);
    Refresh();
}

void ConfigurePlayerAssignment::Refresh() {
    RefreshBuiltin();
    RefreshPlayers();
}

void ConfigurePlayerAssignment::RefreshBuiltin() {
    auto& state = Common::GetControllerConsoleState();
    std::vector<std::pair<std::string, std::string>> pads;
    bool armed = false;
    std::chrono::steady_clock::time_point armed_at{};
    std::string detected_name;
    {
        std::scoped_lock lock{state.mutex};
        pads = state.pads;
        armed = state.identify_armed;
        armed_at = state.identify_armed_at;
        detected_name = state.handheld_name;
    }

    // The entries name the connected controllers; under Steam their data is the Steam handle
    // (every Steam virtual pad shares one GUID), otherwise the pad's identity
    QString signature;
    for (const auto& [display, ident] : pads) {
        signature += QString::fromStdString(ident) + QLatin1Char('|');
    }
    if (signature != builtin_signature) {
        builtin_signature = signature;
        const QString current = builtin_picker->currentData().toString();
        const bool blocked = builtin_picker->blockSignals(true);
        builtin_picker->clear();
        builtin_picker->addItem(tr("Detect automatically"), QString{});
        for (const auto& [display, ident] : pads) {
            builtin_picker->addItem(QString::fromStdString(display),
                                    QString::fromStdString(ident));
        }
        const int keep = builtin_picker->findData(current);
        builtin_picker->setCurrentIndex(keep > 0 ? keep : 0);
        builtin_picker->blockSignals(blocked);
    }

    if (armed) {
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::seconds{15} - (std::chrono::steady_clock::now() - armed_at))
                              .count();
        builtin_status->setText(tr("Press any button on the built-in controls (%1 s)...")
                                    .arg(std::max<qint64>(0, left)));
        builtin_identify->setText(tr("Waiting..."));
        return;
    }
    builtin_identify->setText(tr("Identify..."));

    // What is stored for the way Eden was launched: the Steam handle under Steam, the raw
    // pad's identity otherwise (each launch kind keeps its own)
    const bool steam = LaunchedThroughSteam();
    const QString identity =
        QString::fromStdString(Settings::values.builtin_pad_identity.GetValue());
    const QString handle =
        QString::fromStdString(Settings::values.builtin_pad_steam_handle.GetValue());
    const QString stored = steam ? (handle.isEmpty() ? QString{}
                                                     : QStringLiteral("steamhandle:") + handle)
                                 : identity;
    const bool declared = !steam && Settings::values.builtin_pad_manual.GetValue();

    // Keep the picker on a declared pad, else on automatic detection
    const int stored_index = stored.isEmpty() ? -1 : builtin_picker->findData(stored);
    const int want_index = declared && stored_index > 0 ? stored_index : 0;
    if (builtin_picker->currentIndex() != want_index) {
        const bool blocked = builtin_picker->blockSignals(true);
        builtin_picker->setCurrentIndex(want_index);
        builtin_picker->blockSignals(blocked);
    }

    if (!detected_name.empty()) {
        builtin_status->setText(tr("Using %1. It is player 1 only while no other controller "
                                   "is connected.")
                                    .arg(QString::fromStdString(detected_name)));
    } else if (!stored.isEmpty()) {
        builtin_status->setText(tr("The built-in controls are not connected right now."));
    } else {
        builtin_status->setText(tr("Not detected yet: connect only the built-in controls, or "
                                   "use Identify."));
    }
}

void ConfigurePlayerAssignment::RefreshPlayers() {
    auto& state = Common::GetControllerConsoleState();
    std::vector<Common::ControllerConsoleState::PlayerRow> players;
    std::string handheld_name;
    bool handheld_has_player = false;
    bool engine_active = false;
    {
        std::scoped_lock lock{state.mutex};
        players = state.players;
        handheld_name = state.handheld_name;
        handheld_has_player = state.handheld_has_player;
        engine_active = state.engine_active;
    }
    const auto selected = static_cast<Settings::AppletMode>(mode->currentData().toInt());
    const bool applied =
        selected == Settings::values.controller_applet_mode.GetValue();

    // Rebuild the rows when the players (or the pending swap) change; the highlight alone
    // only restyles them
    QString signature = QString::number(swap_from) + QLatin1Char(';');
    for (const auto& row : players) {
        signature += QString::number(row.player) + QLatin1Char(':') +
                     QString::fromStdString(row.name) + QLatin1Char('|');
    }
    if (!handheld_has_player) {
        signature += QStringLiteral("h:") + QString::fromStdString(handheld_name);
    }
    if (signature != players_signature) {
        players_signature = signature;
        for (QWidget* row : player_rows) {
            players_grid->removeWidget(row);
            row->hide();
            row->deleteLater();
        }
        player_rows.clear();
        swap_buttons.clear();
        swap_button_players.clear();
        int line = 0;
        const auto add = [&](QWidget* widget, int column) {
            players_grid->addWidget(widget, line, column);
            player_rows.push_back(widget);
        };
        for (const auto& row : players) {
            add(new QLabel(QStringLiteral("P%1").arg(row.player + 1), players_box), 0);
            add(new QLabel(QString::fromStdString(row.name), players_box), 1);
            auto* swap = new QPushButton(players_box);
            if (swap_from < 0) {
                swap->setText(tr("Swap"));
                swap->setToolTip(tr("Swap this controller with another player's"));
            } else if (swap_from == row.player) {
                swap->setText(tr("Cancel"));
            } else {
                swap->setText(tr("Swap with P%1").arg(swap_from + 1));
            }
            const int player = row.player;
            connect(swap, &QPushButton::clicked, this, [this, player] { OnSwapPressed(player); });
            add(swap, 2);
            swap_buttons.push_back(swap);
            swap_button_players.push_back(row.player);
            ++line;
        }
        if (!handheld_name.empty() && !handheld_has_player) {
            add(new QLabel(QStringLiteral("—"), players_box), 0);
            add(new QLabel(tr("%1 (built-in, not a player while other controllers are "
                              "connected)")
                               .arg(QString::fromStdString(handheld_name)),
                           players_box),
                1);
            ++line;
        }
    }

    // Light a row while its controller has an input held: pressing a button shows which
    // controller is which player
    for (size_t k = 0; k < swap_buttons.size(); ++k) {
        const auto it = std::find_if(players.begin(), players.end(), [&](const auto& row) {
            return row.player == swap_button_players[k];
        });
        const bool active = it != players.end() && it->active;
        const int index = players_grid->indexOf(swap_buttons[k]);
        int grid_row = 0, column = 0, row_span = 0, column_span = 0;
        players_grid->getItemPosition(index, &grid_row, &column, &row_span, &column_span);
        if (auto* name = players_grid->itemAtPosition(grid_row, 1)) {
            name->widget()->setStyleSheet(active ? QStringLiteral("background-color: rgba(60, "
                                                                  "120, 230, 190); color: white;")
                                                 : QString{});
        }
    }

    if (!applied) {
        players_note->setText(tr("The new assignment takes effect when you press OK."));
    } else if (!engine_active) {
        players_note->setText(tr("Players are assigned in each player's tab."));
    } else if (players.empty()) {
        players_note->setText(tr("No controller is a player right now."));
    } else {
        players_note->setText(tr("Press a button on a controller to see its player. Swaps last "
                                 "for this session and do not change your saved controls."));
    }
    players_box->setVisible(applied && engine_active);
}

void ConfigurePlayerAssignment::OnBuiltinPicked(int index) {
    const QString ident = builtin_picker->itemData(index).toString();
    if (ident.isEmpty()) {
        // Detect automatically: forget the stored pad, so the engine detects it afresh
        Settings::values.builtin_pad_identity.SetValue(std::string{});
        Settings::values.builtin_pad_steam_handle.SetValue(std::string{});
        Settings::values.builtin_pad_manual.SetValue(false);
        return;
    }
    // Under Steam the entries are Steam handles: they set the Steam launches' built-in pad and
    // leave the direct launches' identity alone (each way of launching keeps its own)
    const QString prefix = QStringLiteral("steamhandle:");
    if (ident.startsWith(prefix)) {
        Settings::values.builtin_pad_steam_handle.SetValue(ident.mid(prefix.size()).toStdString());
    } else {
        Settings::values.builtin_pad_identity.SetValue(ident.toStdString());
        Settings::values.builtin_pad_manual.SetValue(true);
    }
}

void ConfigurePlayerAssignment::OnIdentifyPressed() {
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    state.identify_armed = true;
    state.identify_armed_at = std::chrono::steady_clock::now();
}

void ConfigurePlayerAssignment::OnSwapPressed(int player) {
    if (swap_from < 0) {
        swap_from = player;
    } else {
        if (swap_from != player) {
            auto& state = Common::GetControllerConsoleState();
            std::scoped_lock lock{state.mutex};
            state.swap_requests.emplace_back(swap_from, player);
        }
        swap_from = -1;
    }
    RefreshPlayers();
}
