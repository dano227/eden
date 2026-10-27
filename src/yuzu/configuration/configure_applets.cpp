// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <mutex>

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "common/controller_console_state.h"
#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_applets.h"
#include "yuzu/configuration/configuration_shared.h"
#include "yuzu/configuration/configure_applets.h"
#include "yuzu/configuration/shared_widget.h"

ConfigureApplets::ConfigureApplets(Core::System& system_,
                                   std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                                   const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui{std::make_unique<Ui::ConfigureApplets>()}, system{system_} {
    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();
}

ConfigureApplets::~ConfigureApplets() = default;

void ConfigureApplets::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureApplets::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureApplets::Setup(const ConfigurationShared::Builder& builder) {
    auto& library_applets_layout = *ui->group_library_applet_modes->layout();
    std::map<u32, QWidget*> applets_hold{};

    std::vector<Settings::BasicSetting*> settings;
    auto push = [&settings](auto& list) {
        for (auto setting : list) {
            settings.push_back(setting);
        }
    };

    push(Settings::values.linkage.by_category[Settings::Category::LibraryApplet]);

    for (auto setting : settings) {
        ConfigurationShared::Widget* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        // Untested applets
        if (setting->Id() == Settings::values.data_erase_applet_mode.Id() ||
            setting->Id() == Settings::values.net_connect_applet_mode.Id() ||
            setting->Id() == Settings::values.shop_applet_mode.Id() ||
            setting->Id() == Settings::values.login_share_applet_mode.Id() ||
            setting->Id() == Settings::values.wifi_web_auth_applet_mode.Id() ||
            setting->Id() == Settings::values.my_page_applet_mode.Id()) {
            widget->setHidden(true);
        }

        applets_hold.emplace(setting->Id(), widget);
    }
    for (const auto& [label, widget] : applets_hold) {
        library_applets_layout.addWidget(widget);
    }

    // Handheld Console Mode: declare the handheld's own controller instead of guessing
    // it. The picker names the pads that are connected now; Identify captures the next
    // button press; the status line always shows what the engine believes.
    auto* handheld_group = new QGroupBox(tr("Handheld Console Mode"), this);
    auto* handheld_layout = new QVBoxLayout(handheld_group);
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Handheld's controller:"), handheld_group));
    builtin_picker = new QComboBox(handheld_group);
    builtin_picker->addItem(tr("Auto-detect"), QString{});
    row->addWidget(builtin_picker, 1);
    builtin_identify = new QPushButton(tr("Identify handheld controller..."), handheld_group);
    row->addWidget(builtin_identify);
    handheld_layout->addLayout(row);
    builtin_status = new QLabel(handheld_group);
    builtin_status->setWordWrap(true);
    handheld_layout->addWidget(builtin_status);
    library_applets_layout.addWidget(handheld_group);

    connect(builtin_picker, &QComboBox::currentIndexChanged, this,
            &ConfigureApplets::OnBuiltinPicked);
    connect(builtin_identify, &QPushButton::clicked, this,
            &ConfigureApplets::OnIdentifyPressed);

    builtin_timer = new QTimer(this);
    connect(builtin_timer, &QTimer::timeout, this, &ConfigureApplets::RefreshBuiltinPicker);
    builtin_timer->start(500);
    RefreshBuiltinPicker();
}

void ConfigureApplets::RefreshBuiltinPicker() {
    auto& state = Common::GetControllerConsoleState();
    std::vector<std::pair<std::string, std::string>> pads;
    {
        std::scoped_lock lock{state.mutex};
        pads = state.pads;
    }

    QString signature;
    for (const auto& [display, ident] : pads) {
        signature += QString::fromStdString(ident) + QLatin1Char('|');
    }
    const QString current = builtin_picker->currentData().toString();
    if (signature != builtin_signature) {
        builtin_signature = signature;
        const bool blocked = builtin_picker->blockSignals(true);
        builtin_picker->clear();
        builtin_picker->addItem(tr("Auto-detect"), QString{});
        for (const auto& [display, ident] : pads) {
            builtin_picker->addItem(QString::fromStdString(display),
                                    QString::fromStdString(ident));
        }
        int select = 0;
        for (int i = 1; i < builtin_picker->count(); ++i) {
            if (builtin_picker->itemData(i).toString() == current) {
                select = i;
                break;
            }
        }
        builtin_picker->setCurrentIndex(select);
        builtin_picker->blockSignals(blocked);
    }

    {
        std::scoped_lock lock{state.mutex};
        if (state.identify_armed) {
            const auto left = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::seconds{15} -
                                  (std::chrono::steady_clock::now() - state.identify_armed_at))
                                  .count();
            builtin_status->setText(tr("Waiting for a button press (%1 seconds)...")
                                        .arg(std::max<qint64>(0, left)));
            builtin_identify->setText(tr("Press a button on the handheld's pad..."));
            return;
        }
    }

    // The pad the engine holds as the handheld's own, in the picker's terms: under Steam the
    // entries are Steam handles (every pad shares the GUID there), so the stored handle is
    // what names it; a pick the engine has not taken in yet is already a handle entry
    const QString identity =
        QString::fromStdString(Settings::values.builtin_pad_identity.GetValue());
    const QString handle =
        QString::fromStdString(Settings::values.builtin_pad_steam_handle.GetValue());
    const QString stored = identity.startsWith(QStringLiteral("steamhandle:")) || handle.isEmpty()
                               ? identity
                               : QStringLiteral("steamhandle:") + handle;
    const bool manual = Settings::values.builtin_pad_manual.GetValue();
    QString name = tr("not set");
    bool present = false;
    int stored_index = -1;
    for (int i = 1; i < builtin_picker->count(); ++i) {
        if (builtin_picker->itemData(i).toString() == stored) {
            stored_index = i;
            name = builtin_picker->itemText(i);
            present = true;
            break;
        }
    }
    // Keep the picker on what the engine holds: a declared pad, or Auto-detect
    const int want_index = manual ? stored_index : 0;
    if (want_index >= 0 && builtin_picker->currentIndex() != want_index) {
        const bool blocked = builtin_picker->blockSignals(true);
        builtin_picker->setCurrentIndex(want_index);
        builtin_picker->blockSignals(blocked);
    }
    if (stored.isEmpty()) {
        builtin_status->setText(
            tr("Not set. Pick the handheld's controller above, or use Identify."));
    } else {
        builtin_status->setText(
            tr("Handheld: %1 (%2). %3")
                .arg(name)
                .arg(manual ? tr("declared") : tr("auto-detected"))
                .arg(present ? tr("Takes no player while external controllers are connected.")
                             : tr("Not currently connected.")));
    }

    auto& state2 = Common::GetControllerConsoleState();
    bool armed = false;
    {
        std::scoped_lock lock{state2.mutex};
        armed = state2.identify_armed;
    }
    builtin_identify->setText(armed ? tr("Press a button on the handheld's pad...")
                                    : tr("Identify handheld controller..."));
}

void ConfigureApplets::OnBuiltinPicked(int index) {
    const QString ident = builtin_picker->itemData(index).toString();
    if (ident.isEmpty()) {
        // Auto-detect: forget the latched pad entirely, so the engine detects it afresh
        Settings::values.builtin_pad_identity.SetValue(std::string{});
        Settings::values.builtin_pad_steam_handle.SetValue(std::string{});
        Settings::values.builtin_pad_manual.SetValue(false);
    } else {
        Settings::values.builtin_pad_identity.SetValue(ident.toStdString());
        Settings::values.builtin_pad_manual.SetValue(true);
    }
    RefreshBuiltinPicker();
}

void ConfigureApplets::OnIdentifyPressed() {
    auto& state = Common::GetControllerConsoleState();
    {
        std::scoped_lock lock{state.mutex};
        state.identify_armed = true;
        state.identify_armed_at = std::chrono::steady_clock::now();
    }
    builtin_status->setText(tr("Waiting for a button press (15 seconds)..."));
    builtin_identify->setText(tr("Press a button on the handheld's pad..."));
}

void ConfigureApplets::SetConfiguration() {}

void ConfigureApplets::ApplyConfiguration() {
    const bool powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(powered_on);
    }
}
