// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <set>
#include <vector>

#include <QRect>
#include <QString>

class GRenderWindow;
class QEvent;

/// The running game's cheats drawn over the game (top right), so they can be
/// turned on and off also where no window shows over the game (Steam's Game Mode). Its arrow
/// button tucks it away against the screen's right edge, where its menu button brings it back.
/// Its eye button chooses which cheats show (remembered per game). Drawn as an overlay panel.
class CheatOverlay {
public:
    struct Cheat {
        QString name;
        bool enabled = false;
        /// A master code: always on, can't be turned off
        bool locked = false;
    };

    /// Turns a cheat (its index in the list) on or off: the menu's own action does it, so the
    /// menu and what is remembered stay in step
    explicit CheatOverlay(std::function<void(int)> toggle_cheat);

    /// What to show: the game's ids and its cheats; hidden when not shown or with no cheats
    void Update(bool shown, const QString& game_id, const QString& build_id,
                std::vector<Cheat> cheats);

    /// The mouse over the panel (hover, clicks, the wheel to scroll): true when it took the
    /// event, so the game doesn't get it
    bool HandleMouse(QEvent* event, const GRenderWindow& window);

private:
    void Render();
    /// Moves the cheats in view by some rows (within the list)
    void ScrollBy(int rows);
    /// The cheats listed (their indices): the shown ones, or every one while choosing
    [[nodiscard]] std::vector<int> Listed() const;

    std::function<void(int)> toggle_cheat;
    bool shown = false;
    bool collapsed = false;
    /// Choosing which cheats show (the eye button): every cheat listed, each with its eye
    bool choosing = false;
    /// The cheats hidden from the panel (by name), for this game
    std::set<QString> hidden;
    QString game_id;
    QString build_id;
    std::vector<Cheat> cheats;
    /// The first cheat in view, when there are more than fit
    int scroll = 0;
    /// The cheat (or a button: menu, scroll up, scroll down) under the cursor, and the one
    /// pressed; -1 none
    int hover = -1;
    int pressed = -1;

    /// Where things are in the drawn image, for the mouse
    QRect menu_rect;
    QRect up_rect;
    QRect down_rect;
    QRect eye_rect;
    std::vector<std::pair<int, QRect>> cheat_rects;
    /// What was drawn last, to draw again only when it changes
    QString drawn;
};
