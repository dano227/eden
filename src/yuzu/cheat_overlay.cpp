// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QFontMetrics>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStringList>
#include <QWheelEvent>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/overlay_panels.h"
#include "yuzu/cheat_overlay.h"
#include "yuzu/overlay_panel.h"

namespace {

constexpr int PanelWidth = 300;
constexpr int HeaderHeight = 30;
constexpr int InfoHeight = 38;
constexpr int RowHeight = 26;
constexpr int Gap = 4;
constexpr int Inner = 8;
/// The most cheats in view at once; the wheel scrolls through the rest
constexpr int VisibleRows = 9;
/// The buttons stand for themselves in the hover and press state
constexpr int MenuButton = -2;
constexpr int ScrollUp = -3;
constexpr int ScrollDown = -4;
constexpr int EyeButton = -5;
constexpr int FooterHeight = 22;

/// The cheats hidden from the overlay, per game (by cheat name)
QString HiddenFile() {
    return QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "cheat_overlay.ini"));
}

Common::OverlayPanel& Panel() {
    return Common::GetOverlayPanel(Common::OverlayPanelId::Cheats);
}

/// The menu button's icon: three bars
void MenuIcon(QPainter& painter, const QRectF& rect) {
    painter.setPen(QPen(QColor(245, 245, 245), 2.0, Qt::SolidLine, Qt::RoundCap));
    const qreal left = rect.center().x() - 6;
    const qreal right = rect.center().x() + 6;
    for (int bar = -1; bar <= 1; ++bar) {
        const qreal y = rect.center().y() + bar * 4.5;
        painter.drawLine(QPointF(left, y), QPointF(right, y));
    }
    painter.setPen(Qt::NoPen);
}

/// An arrow head pointing right (tuck away), up or down (scroll), in the pen colour given
void ArrowIcon(QPainter& painter, const QRectF& rect, Qt::ArrowType arrow, const QColor& color) {
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QPointF c = rect.center();
    constexpr qreal s = 4.0;
    if (arrow == Qt::RightArrow) {
        const QPointF points[3] = {{c.x() - s / 2, c.y() - s}, {c.x() + s / 2, c.y()},
                                   {c.x() - s / 2, c.y() + s}};
        painter.drawPolyline(points, 3);
    } else {
        const qreal d = arrow == Qt::UpArrow ? -1.0 : 1.0;
        const QPointF points[3] = {{c.x() - s, c.y() - d * s / 2}, {c.x(), c.y() + d * s / 2},
                                   {c.x() + s, c.y() - d * s / 2}};
        painter.drawPolyline(points, 3);
    }
    painter.setPen(Qt::NoPen);
}

/// An eye (shown), or an eye struck through (hidden)
void EyeIcon(QPainter& painter, const QRectF& rect, bool open, const QColor& color) {
    const QPointF c = rect.center();
    painter.setPen(QPen(color, 1.6));
    painter.setBrush(Qt::NoBrush);
    QPainterPath eye;
    eye.moveTo(c.x() - 7, c.y());
    eye.quadTo(c.x(), c.y() - 7, c.x() + 7, c.y());
    eye.quadTo(c.x(), c.y() + 7, c.x() - 7, c.y());
    painter.drawPath(eye);
    painter.setBrush(color);
    painter.drawEllipse(c, 2.0, 2.0);
    if (!open) {
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(c.x() - 7, c.y() + 6), QPointF(c.x() + 7, c.y() - 6));
    }
    painter.setPen(Qt::NoPen);
}

} // Anonymous namespace

CheatOverlay::CheatOverlay(std::function<void(int)> toggle_cheat_)
    : toggle_cheat{std::move(toggle_cheat_)} {}

void CheatOverlay::Update(bool shown_, const QString& game_id_, const QString& build_id_,
                          std::vector<Cheat> cheats_) {
    shown = shown_;
    if (!game_id_.isEmpty() && game_id_ != game_id) {
        // Another game: its own hidden cheats
        QSettings settings{HiddenFile(), QSettings::IniFormat};
        const QStringList names =
            settings.value(QStringLiteral("hidden/%1").arg(game_id_)).toStringList();
        hidden = {names.begin(), names.end()};
        choosing = false;
        scroll = 0;
    }
    if (!game_id_.isEmpty()) {
        game_id = game_id_;
    }
    build_id = build_id_;
    cheats = std::move(cheats_);
    scroll = (std::clamp)(scroll, 0,
                          (std::max)(0, static_cast<int>(Listed().size()) - VisibleRows));
    if (!shown || cheats.empty()) {
        hover = -1;
        pressed = -1;
    }
    Render();
}

bool CheatOverlay::HandleMouse(QEvent* event, const GRenderWindow& window) {
    const auto type = event->type();
    if (type != QEvent::MouseMove && type != QEvent::MouseButtonPress &&
        type != QEvent::MouseButtonRelease && type != QEvent::MouseButtonDblClick &&
        type != QEvent::Wheel) {
        return false;
    }
    if (!GameOverlay::Shown(Panel())) {
        return false;
    }
    const auto cursor = GameOverlay::CursorOn(Panel(), window);
    if (!cursor.inside) {
        if (type == QEvent::MouseButtonRelease) {
            pressed = -1;
        }
        if (hover != -1) {
            hover = -1;
            Render();
        }
        return false;
    }
    int target = -1;
    if (menu_rect.contains(cursor.at)) {
        target = MenuButton;
    } else if (up_rect.contains(cursor.at)) {
        target = ScrollUp;
    } else if (down_rect.contains(cursor.at)) {
        target = ScrollDown;
    } else if (eye_rect.contains(cursor.at)) {
        target = EyeButton;
    }
    for (const auto& [index, rect] : cheat_rects) {
        if (rect.contains(cursor.at)) {
            target = index;
        }
    }
    switch (type) {
    case QEvent::MouseMove:
        if (hover != target) {
            hover = target;
            Render();
        }
        break;
    case QEvent::MouseButtonPress:
        pressed = target;
        break;
    case QEvent::MouseButtonRelease:
        if (pressed == target && target == MenuButton) {
            collapsed = !collapsed;
            hover = -1;
            Render();
        } else if (pressed == target && (target == ScrollUp || target == ScrollDown)) {
            // A page at a time
            ScrollBy(target == ScrollUp ? -VisibleRows : VisibleRows);
        } else if (pressed == target && target == EyeButton) {
            // Choosing what shows lists every cheat; done, only the shown ones
            choosing = !choosing;
            scroll = 0;
            hover = -1;
            Render();
        } else if (pressed == target && target >= 0 &&
                   target < static_cast<int>(cheats.size())) {
            const auto& cheat = cheats[static_cast<std::size_t>(target)];
            if (choosing) {
                if (!hidden.erase(cheat.name)) {
                    hidden.insert(cheat.name);
                }
                QSettings settings{HiddenFile(), QSettings::IniFormat};
                const QStringList names{hidden.begin(), hidden.end()};
                if (names.isEmpty()) {
                    settings.remove(QStringLiteral("hidden/%1").arg(game_id));
                } else {
                    settings.setValue(QStringLiteral("hidden/%1").arg(game_id), names);
                }
                Render();
            } else if (!cheat.locked) {
                // (The menu's action redraws the panel through Update)
                toggle_cheat(target);
            }
        }
        pressed = -1;
        break;
    case QEvent::Wheel:
        if (!collapsed) {
            ScrollBy(-static_cast<QWheelEvent*>(event)->angleDelta().y() / 120);
        }
        break;
    default:
        break;
    }
    // (Over the panel, the game gets no touch)
    event->accept();
    return true;
}

std::vector<int> CheatOverlay::Listed() const {
    std::vector<int> listed;
    for (int i = 0; i < static_cast<int>(cheats.size()); ++i) {
        if (choosing || !hidden.contains(cheats[static_cast<std::size_t>(i)].name)) {
            listed.push_back(i);
        }
    }
    return listed;
}

void CheatOverlay::ScrollBy(int rows) {
    const int last = (std::max)(0, static_cast<int>(Listed().size()) - VisibleRows);
    const int moved = (std::clamp)(scroll + rows, 0, last);
    if (moved != scroll) {
        scroll = moved;
        Render();
    }
}

void CheatOverlay::Render() {
    auto& panel = Panel();
    if (!shown || cheats.empty()) {
        if (!drawn.isEmpty()) {
            GameOverlay::Publish(panel, nullptr);
            drawn.clear();
        }
        return;
    }
    // Drawn again only when what it shows changes (each image is uploaded again)
    QString state = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                        .arg(static_cast<int>(collapsed))
                        .arg(static_cast<int>(choosing))
                        .arg(hover)
                        .arg(scroll)
                        .arg(game_id, build_id)
                        .arg(cheats.size());
    for (const auto& cheat : cheats) {
        state += QStringLiteral("|%1%2%3%4")
                     .arg(static_cast<int>(cheat.enabled))
                     .arg(static_cast<int>(cheat.locked))
                     .arg(cheat.name)
                     .arg(static_cast<int>(hidden.contains(cheat.name)));
    }
    if (state == drawn) {
        return;
    }
    drawn = state;

    namespace Style = GameOverlay::Style;
    const QFont title_font = Style::LabelFont();
    const QFont text_font = Style::TextFont();
    const QFont small_font = Style::SmallFont();
    const QFontMetrics text_fm{text_font};
    const int on_count = static_cast<int>(
        std::count_if(cheats.begin(), cheats.end(), [](const Cheat& cheat) {
            return cheat.enabled;
        }));
    const QColor hover_fill(45, 95, 200, 150);

    menu_rect = {};
    up_rect = {};
    down_rect = {};
    eye_rect = {};
    cheat_rects.clear();

    // Tucked away: only the menu button, against the screen's edge (a green dot while any
    // cheat is on)
    if (collapsed) {
        QImage image{HeaderHeight + 12, HeaderHeight + 2, QImage::Format_RGBA8888};
        image.fill(Qt::transparent);
        QPainter painter{&image};
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF pill(1, 1, HeaderHeight + 10, HeaderHeight);
        Style::Pill(painter, pill, hover == MenuButton ? hover_fill : Style::Fill);
        MenuIcon(painter, pill);
        if (on_count > 0) {
            painter.setBrush(QColor(120, 220, 140));
            painter.drawEllipse(QPointF(pill.right() - 7, pill.top() + 7), 3.5, 3.5);
        }
        painter.end();
        menu_rect = pill.toRect();
        GameOverlay::Publish(panel, &image);
        return;
    }

    const int count = static_cast<int>(cheats.size());
    const std::vector<int> listed = Listed();
    const int listed_count = static_cast<int>(listed.size());
    const int first = scroll;
    const int last = (std::min)(listed_count, first + VisibleRows);
    const bool more = listed_count > VisibleRows;
    // (Every cheat hidden: a line that says so)
    const int rows = (std::max)(1, last - first);
    const int rows_top = HeaderHeight + Gap + InfoHeight + Gap;
    const int footer_top = rows_top + rows * (RowHeight + Gap);
    const int height = (more ? footer_top + FooterHeight : footer_top - Gap) + 2;
    const int width = PanelWidth + 2;

    QImage image{width, height, QImage::Format_RGBA8888};
    image.fill(Qt::transparent);
    QPainter painter{&image};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // The header: the button that tucks it away, the title and how many are on
    const QRectF header(1, 1, PanelWidth, HeaderHeight);
    Style::Pill(painter, header, Style::Fill);
    const QRectF menu(header.left() + 4, header.top() + 4, HeaderHeight - 8 + 6,
                      HeaderHeight - 8);
    Style::Pill(painter, menu, hover == MenuButton ? hover_fill : QColor(255, 255, 255, 30));
    ArrowIcon(painter, menu, Qt::RightArrow, QColor(245, 245, 245));
    menu_rect = menu.toRect();
    painter.setFont(title_font);
    painter.setPen(Style::Text);
    painter.drawText(QRectF(menu.right() + 8, header.top(), 120, header.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, QObject::tr("Cheats"));
    // The eye: choose which cheats show (lit while choosing)
    const QRectF eye(header.right() - 4 - 30, header.top() + 4, 30, HeaderHeight - 8);
    Style::Pill(painter, eye,
                choosing || hover == EyeButton ? hover_fill : QColor(255, 255, 255, 30));
    EyeIcon(painter, eye, true, QColor(245, 245, 245));
    eye_rect = eye.toRect();
    painter.setFont(small_font);
    const QRectF count_rect = header.adjusted(0, 0, -(eye.width() + 4 + 8), 0);
    if (choosing) {
        painter.setPen(Style::DimText);
        painter.drawText(count_rect, Qt::AlignVCenter | Qt::AlignRight,
                         QObject::tr("Tap to hide or show"));
    } else {
        painter.setPen(on_count > 0 ? QColor(120, 220, 140) : Style::DimText);
        painter.drawText(count_rect, Qt::AlignVCenter | Qt::AlignRight,
                         QObject::tr("%1 of %2 on").arg(on_count).arg(count));
    }

    // The game: its title id and the build id its cheat files are named after
    const QRectF info(1, header.bottom() + Gap, PanelWidth, InfoHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Style::Fill);
    painter.drawRoundedRect(info, 12, 12);
    const auto info_line = [&](int line, const QString& label, const QString& value) {
        const QRectF at(info.left() + Inner + 4, info.top() + 3 + line * 16,
                        info.width() - (Inner + 4) * 2, 16);
        painter.setFont(small_font);
        painter.setPen(Style::DimText);
        painter.drawText(at, Qt::AlignVCenter | Qt::AlignLeft, label);
        painter.setPen(QColor(225, 225, 230));
        painter.drawText(at, Qt::AlignVCenter | Qt::AlignRight, value);
    };
    info_line(0, QObject::tr("Game ID"), game_id);
    info_line(1, QObject::tr("Build ID"), build_id);

    // The cheats: a pill each, with its switch (or, while choosing, whether it shows)
    constexpr int switch_w = 34;
    constexpr int switch_h = 18;
    if (listed.empty()) {
        const QRectF row(1, rows_top, PanelWidth, RowHeight);
        Style::Pill(painter, row, Style::Fill);
        painter.setFont(text_font);
        painter.setPen(Style::DimText);
        painter.drawText(row, Qt::AlignCenter, QObject::tr("All cheats are hidden"));
    }
    for (int k = first; k < last; ++k) {
        const int i = listed[static_cast<std::size_t>(k)];
        const auto& cheat = cheats[static_cast<std::size_t>(i)];
        const bool is_hidden = hidden.contains(cheat.name);
        const int y = rows_top + (k - first) * (RowHeight + Gap);
        const QRectF row(1, y, PanelWidth, RowHeight);
        Style::Pill(painter, row,
                    hover == i && (choosing || !cheat.locked) ? hover_fill : Style::Fill);
        const QRectF toggle(row.right() - Inner - switch_w, row.center().y() - switch_h / 2.0,
                            switch_w, switch_h);
        if (choosing) {
            EyeIcon(painter, toggle, !is_hidden,
                    is_hidden ? Style::DimText : QColor(245, 245, 245));
        } else {
            Style::Switch(painter, toggle, cheat.enabled || cheat.locked, cheat.locked);
        }
        painter.setFont(text_font);
        painter.setPen(cheat.locked || (choosing && is_hidden) ? Style::DimText : Style::Text);
        const int name_w = static_cast<int>(toggle.left() - row.left()) - Inner * 2 - 6;
        const QString name = cheat.locked ? QObject::tr("%1 (master code)").arg(cheat.name)
                                          : cheat.name;
        painter.drawText(QRectF(row.left() + Inner + 4, row.top(), name_w, row.height()),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         text_fm.elidedText(name, Qt::ElideRight, name_w));
        cheat_rects.emplace_back(i, row.toRect());
    }

    // More than fit: which ones are in view, between buttons that page up and down (the
    // wheel scrolls too)
    if (more) {
        const QRectF footer(PanelWidth / 2.0 - 80, footer_top, 160, FooterHeight);
        Style::Pill(painter, footer, Style::Fill);
        const auto scroll_button = [&](const QRectF& rect, int which, Qt::ArrowType arrow,
                                       bool usable) {
            Style::Pill(painter, rect,
                        hover == which && usable ? hover_fill : QColor(255, 255, 255, 30));
            ArrowIcon(painter, rect, arrow, usable ? QColor(245, 245, 245) : Style::DimText);
            return usable ? rect.toRect() : QRect{};
        };
        up_rect = scroll_button(QRectF(footer.left() + 3, footer.top() + 3, 34, FooterHeight - 6),
                                ScrollUp, Qt::UpArrow, first > 0);
        down_rect = scroll_button(
            QRectF(footer.right() - 3 - 34, footer.top() + 3, 34, FooterHeight - 6), ScrollDown,
            Qt::DownArrow, last < listed_count);
        painter.setFont(small_font);
        painter.setPen(Style::DimText);
        painter.drawText(footer, Qt::AlignCenter,
                         QStringLiteral("%1–%2 / %3").arg(first + 1).arg(last).arg(listed_count));
    }
    painter.end();
    GameOverlay::Publish(panel, &image);
}
