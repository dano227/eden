// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QString>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QTimer;
class QWidget;

/// The Players tab of the Controls settings: how players are assigned (by hand, or
/// automatically from the connected controllers), which controller is the handheld's own
/// built-in one, and the live list of players with a swap per row.
class ConfigurePlayerAssignment : public QWidget {
    Q_OBJECT

public:
    explicit ConfigurePlayerAssignment(QWidget* parent = nullptr);
    ~ConfigurePlayerAssignment() override;

    void ApplyConfiguration();

private:
    void LoadConfiguration();
    /// The controller applet mode the controls select
    int SelectedMode() const;
    void OnModeChanged();
    void UpdateModeUi();
    void Refresh();
    void RefreshBuiltin();
    void RefreshPlayers();
    void OnBuiltinPicked(int index);
    void OnIdentifyPressed();
    void OnSwapPressed(int player);

    /// Custom frontend or Real applet; the frontend's modes are radio buttons below it
    QComboBox* mode = nullptr;
    QWidget* frontend_row = nullptr;
    QButtonGroup* frontend_modes = nullptr;

    QWidget* builtin_row = nullptr;
    QComboBox* builtin_picker = nullptr;
    QPushButton* builtin_identify = nullptr;
    QLabel* builtin_status = nullptr;
    QString builtin_signature;

    QWidget* players_box = nullptr;
    QGridLayout* players_grid = nullptr;
    QLabel* players_note = nullptr;
    QString players_signature;
    /// The player whose Swap was pressed first, waiting for the second (-1: none)
    int swap_from = -1;
    std::vector<QPushButton*> swap_buttons;
    std::vector<int> swap_button_players;
    std::vector<QWidget*> player_rows;

    QTimer* timer = nullptr;
    /// The mode when the dialog opened, put back unless the dialog applies
    int original_mode = 0;
    bool applied = false;
};
