// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>

#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QMimeData>
#include <QMouseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QRadioButton>
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

constexpr int MaxPlayers = 8;
constexpr char PlayerMime[] = "application/x-eden-player";

/// One player in the list: a click rumbles its controller, and a long press (or a drag) picks
/// it up to drop on another player, which swaps the two
class PlayerRow : public QFrame {
public:
    PlayerRow(int player_, QWidget* parent) : QFrame(parent), player{player_} {
        setObjectName(QStringLiteral("playerRow"));
        setAcceptDrops(true);
        long_press.setSingleShot(true);
        long_press.setInterval(400);
        QObject::connect(&long_press, &QTimer::timeout, this, [this] { StartDrag(); });
    }

    std::function<void(int)> on_click;
    std::function<void(int, int)> on_drop;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            press_pos = event->position().toPoint();
            pressed = true;
            long_press.start();
        }
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressed && (event->position().toPoint() - press_pos).manhattanLength() >=
                           QApplication::startDragDistance()) {
            StartDrag();
        }
        QFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (pressed && long_press.isActive() && on_click) {
            on_click(player);
        }
        pressed = false;
        long_press.stop();
        QFrame::mouseReleaseEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasFormat(QString::fromLatin1(PlayerMime))) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override {
        const int from = event->mimeData()->data(QString::fromLatin1(PlayerMime)).toInt();
        if (from != player && on_drop) {
            on_drop(from, player);
        }
        event->acceptProposedAction();
    }

private:
    void StartDrag() {
        if (!pressed) {
            return;
        }
        pressed = false;
        long_press.stop();
        auto* mime = new QMimeData;
        mime->setData(QString::fromLatin1(PlayerMime), QByteArray::number(player));
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        drag->exec(Qt::MoveAction);
    }

    int player;
    bool pressed = false;
    QPoint press_pos;
    QTimer long_press;
};

} // Anonymous namespace

ConfigurePlayerAssignment::ConfigurePlayerAssignment(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // The controller applet's mode: the real applet, or one of the custom frontend's modes
    auto* mode_box = new QGroupBox(tr("Controller Configuration"), this);
    auto* mode_layout = new QVBoxLayout(mode_box);
    mode = new QComboBox(mode_box);
    mode->addItem(tr("Custom"));
    mode->addItem(tr("Real Hardware"));
    mode_layout->addWidget(mode);

    frontend_row = new QWidget(mode_box);
    auto* frontend_layout = new QHBoxLayout(frontend_row);
    frontend_layout->setContentsMargins(0, 0, 0, 0);
    frontend_modes = new QButtonGroup(this);
    const auto add_mode = [&](const QString& name, const QString& tooltip,
                              Settings::AppletMode value) {
        auto* button = new QRadioButton(name, frontend_row);
        button->setToolTip(tooltip);
        frontend_modes->addButton(button, static_cast<int>(value));
        frontend_layout->addWidget(button);
    };
    add_mode(tr("Modern"), tr("Each controller that connects becomes the next player."),
             Settings::AppletMode::ConsoleMode);
    add_mode(tr("Handheld"),
             tr("The handheld's controls are player 1 only while no other controller is "
                "connected."),
             Settings::AppletMode::HandheldConsoleMode);
    add_mode(tr("Disabled"),
             tr("Players are set in each player's tab; games get them without a window."),
             Settings::AppletMode::Disabled);
    add_mode(tr("Legacy Applet"),
             tr("Players are set in each player's tab; games open Eden's controller window."),
             Settings::AppletMode::HLE);
    frontend_layout->addStretch(1);
    mode_layout->addWidget(frontend_row);
    mode_info = new QLabel(mode_box);
    mode_info->setWordWrap(true);
    mode_layout->addWidget(mode_info);
    layout->addWidget(mode_box);

    // Handheld Console Mode: declare the handheld's own controller instead of guessing it.
    // Shown for that mode only.
    builtin_row = new QGroupBox(tr("Handheld Console Mode"), this);
    auto* builtin_layout = new QVBoxLayout(builtin_row);
    auto* picker_row = new QHBoxLayout();
    picker_row->addWidget(new QLabel(tr("Handheld's controller:"), builtin_row));
    builtin_picker = new QComboBox(builtin_row);
    builtin_picker->addItem(tr("Auto-detect"), QString{});
    picker_row->addWidget(builtin_picker, 1);
    builtin_identify = new QPushButton(tr("Identify handheld controller..."), builtin_row);
    picker_row->addWidget(builtin_identify);
    builtin_layout->addLayout(picker_row);
    builtin_status = new QLabel(builtin_row);
    builtin_status->setWordWrap(true);
    builtin_layout->addWidget(builtin_status);
    layout->addWidget(builtin_row);

    // The players right now, as the engine assigned them
    now_box = new QGroupBox(tr("Players now"), this);
    auto* now_layout = new QVBoxLayout(now_box);
    players_box = new QWidget(now_box);
    players_list = new QVBoxLayout(players_box);
    players_list->setContentsMargins(0, 0, 0, 0);
    players_list->setSpacing(2);
    now_layout->addWidget(players_box);
    players_note = new QLabel(now_box);
    players_note->setWordWrap(true);
    now_layout->addWidget(players_note);
    layout->addWidget(now_box);
    layout->addStretch(1);

    // The mode applies at once, so the players switch while the dialog is open; closing it
    // without applying puts the previous mode back (see the destructor)
    original_mode = static_cast<int>(Settings::values.controller_applet_mode.GetValue());
    connect(mode, &QComboBox::currentIndexChanged, this,
            &ConfigurePlayerAssignment::OnModeChanged);
    connect(frontend_modes, &QButtonGroup::idClicked, this,
            &ConfigurePlayerAssignment::OnModeChanged);
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

ConfigurePlayerAssignment::~ConfigurePlayerAssignment() {
    if (!applied) {
        Settings::values.controller_applet_mode.SetValue(
            static_cast<Settings::AppletMode>(original_mode));
    }
}

void ConfigurePlayerAssignment::LoadConfiguration() {
    const auto current = Settings::values.controller_applet_mode.GetValue();
    const bool real_applet = current == Settings::AppletMode::LLE;
    // The real applet keeps Legacy Applet selected underneath, for switching back
    auto* button = frontend_modes->button(
        static_cast<int>(real_applet ? Settings::AppletMode::HLE : current));
    if (!button) {
        button = frontend_modes->button(static_cast<int>(Settings::AppletMode::HLE));
    }
    const bool blocked = mode->blockSignals(true);
    button->setChecked(true);
    mode->setCurrentIndex(real_applet ? 1 : 0);
    mode->blockSignals(blocked);
    UpdateModeUi();
}

int ConfigurePlayerAssignment::SelectedMode() const {
    if (mode->currentIndex() == 1) {
        return static_cast<int>(Settings::AppletMode::LLE);
    }
    return frontend_modes->checkedId();
}

void ConfigurePlayerAssignment::OnModeChanged() {
    Settings::values.controller_applet_mode.SetValue(
        static_cast<Settings::AppletMode>(SelectedMode()));
    UpdateModeUi();
}

void ConfigurePlayerAssignment::ApplyConfiguration() {
    Settings::values.controller_applet_mode.SetValue(
        static_cast<Settings::AppletMode>(SelectedMode()));
    applied = true;
}

void ConfigurePlayerAssignment::UpdateModeUi() {
    const auto selected = static_cast<Settings::AppletMode>(SelectedMode());
    frontend_row->setVisible(mode->currentIndex() == 0);
    // Only the automatic modes have live players to show
    const bool automatic = IsAutomatic(selected);
    switch (selected) {
    case Settings::AppletMode::ConsoleMode:
        mode_info->setText(tr("Controller assignment is automatic. Each controller is assigned "
                              "to the next available player when it connects."));
        break;
    case Settings::AppletMode::HandheldConsoleMode:
        mode_info->setText(tr("Controller assignment is automatic. Connected controllers take "
                              "priority over the handheld's built-in controls, which are used "
                              "only when no other controller is connected."));
        break;
    case Settings::AppletMode::Disabled:
        mode_info->setText(tr("Controllers are assigned in each player's tab. Games use the "
                              "configured players without showing a controller applet."));
        break;
    case Settings::AppletMode::LLE:
        mode_info->setText(tr("Controllers are assigned in each player's tab. When a game "
                              "requests controller setup, the console's own controller applet "
                              "is shown. This requires the system firmware."));
        break;
    default:
        mode_info->setText(tr("Controllers are assigned in each player's tab. When a game "
                              "requests controller setup, Eden's controller applet is shown."));
        break;
    }
    now_box->setVisible(automatic);
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
        builtin_picker->addItem(tr("Auto-detect"), QString{});
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
        builtin_status->setText(tr("Waiting for a button press (%1 seconds)...")
                                    .arg(std::max<qint64>(0, left)));
        builtin_identify->setText(tr("Press a button on the handheld's pad..."));
        return;
    }
    builtin_identify->setText(tr("Identify handheld controller..."));

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

    if (stored.isEmpty() && detected_name.empty()) {
        builtin_status->setText(
            tr("Not set. Pick the handheld's controller above, or use Identify."));
    } else {
        const QString name = stored_index > 0 ? builtin_picker->itemText(stored_index)
                             : !detected_name.empty() ? QString::fromStdString(detected_name)
                                                      : tr("not set");
        builtin_status->setText(
            tr("Handheld: %1 (%2). %3")
                .arg(name)
                .arg(want_index > 0 ? tr("declared") : tr("auto-detected"))
                .arg(!detected_name.empty()
                         ? tr("Takes no player while external controllers are connected.")
                         : tr("Not currently connected.")));
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
    const auto selected = static_cast<Settings::AppletMode>(SelectedMode());
    const bool in_effect = selected == Settings::values.controller_applet_mode.GetValue();

    // Rebuild the rows when the players (or the pending swap) change; activity only restyles
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
        for (QFrame* row : player_rows) {
            players_list->removeWidget(row);
            row->hide();
            row->deleteLater();
        }
        player_rows.clear();
        row_players.clear();
        for (const auto& entry : players) {
            const int player = entry.player;
            auto* row = new PlayerRow(player, players_box);
            row->setToolTip(tr("Click to rumble this controller. Hold, then drop on another "
                               "player to swap them."));
            row->on_click = [this](int p) { RequestRumble(p); };
            row->on_drop = [this](int from, int to) { RequestSwap(from, to); };
            auto* line = new QHBoxLayout(row);
            line->setContentsMargins(4, 2, 4, 2);
            line->addWidget(new QLabel(QStringLiteral("P%1").arg(player + 1), row));
            line->addWidget(new QLabel(QString::fromStdString(entry.name), row), 1);

            auto* up = new QPushButton(QStringLiteral("\u25B2"), row);
            up->setToolTip(tr("Swap with P%1").arg(player));
            up->setEnabled(player > 0);
            connect(up, &QPushButton::clicked, this,
                    [this, player] { RequestSwap(player, player - 1); });
            line->addWidget(up);
            auto* down = new QPushButton(QStringLiteral("\u25BC"), row);
            down->setToolTip(tr("Swap with P%1").arg(player + 2));
            down->setEnabled(player + 1 < MaxPlayers);
            connect(down, &QPushButton::clicked, this,
                    [this, player] { RequestSwap(player, player + 1); });
            line->addWidget(down);
            for (auto* arrow : {up, down}) {
                arrow->setFixedWidth(arrow->sizeHint().height() + 6);
            }

            auto* swap = new QPushButton(row);
            if (swap_from < 0) {
                swap->setText(tr("Swap"));
            } else if (swap_from == player) {
                swap->setText(tr("Cancel"));
            } else {
                swap->setText(tr("Swap with P%1").arg(swap_from + 1));
            }
            connect(swap, &QPushButton::clicked, this, [this, player] { OnSwapPressed(player); });
            line->addWidget(swap);

            auto* settings = new QPushButton(tr("Settings"), row);
            settings->setToolTip(tr("Open Player %1's tab").arg(player + 1));
            settings->setEnabled(static_cast<bool>(open_player_tab));
            connect(settings, &QPushButton::clicked, this, [this, player] {
                if (open_player_tab) {
                    open_player_tab(player);
                }
            });
            line->addWidget(settings);

            players_list->addWidget(row);
            player_rows.push_back(row);
            row_players.push_back(player);
        }
        if (!handheld_name.empty() && !handheld_has_player) {
            auto* row = new QFrame(players_box);
            auto* line = new QHBoxLayout(row);
            line->setContentsMargins(4, 2, 4, 2);
            line->addWidget(new QLabel(QStringLiteral("\u2014"), row));
            line->addWidget(new QLabel(tr("%1 (built-in, not a player while other controllers "
                                          "are connected)")
                                           .arg(QString::fromStdString(handheld_name)),
                                       row),
                            1);
            players_list->addWidget(row);
            player_rows.push_back(row);
            row_players.push_back(-1);
        }
    }

    // A row lights while its controller has an input held: pressing a button shows which
    // controller is which player
    for (size_t k = 0; k < player_rows.size(); ++k) {
        const auto it = std::find_if(players.begin(), players.end(), [&](const auto& row) {
            return row.player == row_players[k];
        });
        const bool active = it != players.end() && it->active;
        player_rows[k]->setStyleSheet(
            active ? QStringLiteral("QFrame#playerRow { background-color: rgba(60, 120, 230, "
                                    "190); } QFrame#playerRow QLabel { color: white; }")
                   : QString{});
    }

    if (!in_effect || (IsAutomatic(selected) && !engine_active)) {
        players_note->setText(tr("Switching..."));
    } else if (players.empty()) {
        players_note->setText(tr("No controller is a player right now."));
    } else {
        players_note->setText(tr("Press a button on a controller to see its player. Swaps last "
                                 "for this session and do not change your saved controls."));
    }
    players_box->setVisible(in_effect && engine_active);
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
            RequestSwap(swap_from, player);
        }
        swap_from = -1;
    }
    RefreshPlayers();
}

void ConfigurePlayerAssignment::RequestSwap(int a, int b) {
    if (a == b || a < 0 || b < 0 || a >= MaxPlayers || b >= MaxPlayers) {
        return;
    }
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    state.swap_requests.emplace_back(a, b);
}

void ConfigurePlayerAssignment::RequestRumble(int player) {
    auto& state = Common::GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    state.rumble_requests.push_back(player);
}

void ConfigurePlayerAssignment::SetOpenPlayerTab(std::function<void(int)> open) {
    open_player_tab = std::move(open);
    players_signature.clear();
    RefreshPlayers();
}
