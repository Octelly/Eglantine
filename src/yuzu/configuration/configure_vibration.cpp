// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "ui_configure_vibration.h"
#include "yuzu/configuration/configure_vibration.h"
#include "input_common/drivers/renhdrum_pad.h"
#include "yuzu/configuration/configure_hd_rumble.h"

#include <QLabel>
#include <QPushButton>

ConfigureVibration::ConfigureVibration(QWidget* parent, Core::HID::HIDCore& hid_core_)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigureVibration>()), hid_core{hid_core_} {
    ui->setupUi(this);

    vibration_groupboxes = {
        ui->vibrationGroupPlayer1, ui->vibrationGroupPlayer2, ui->vibrationGroupPlayer3,
        ui->vibrationGroupPlayer4, ui->vibrationGroupPlayer5, ui->vibrationGroupPlayer6,
        ui->vibrationGroupPlayer7, ui->vibrationGroupPlayer8,
    };

    vibration_spinboxes = {
        ui->vibrationSpinPlayer1, ui->vibrationSpinPlayer2, ui->vibrationSpinPlayer3,
        ui->vibrationSpinPlayer4, ui->vibrationSpinPlayer5, ui->vibrationSpinPlayer6,
        ui->vibrationSpinPlayer7, ui->vibrationSpinPlayer8,
    };

    const auto& players = Settings::values.players.GetValue();

    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        auto controller = hid_core.GetEmulatedControllerByIndex(i);
        Core::HID::ControllerUpdateCallback engine_callback{
            .on_change = [this,
                          i](Core::HID::ControllerTriggerType type) { VibrateController(type, i); },
            .is_npad_service = false,
        };
        controller_callback_key[i] = controller->SetCallback(engine_callback);
        vibration_groupboxes[i]->setChecked(players[i].vibration_enabled);
        vibration_spinboxes[i]->setValue(players[i].vibration_strength);
    }

    ui->checkBoxAccurateVibration->setChecked(
        Settings::values.enable_accurate_vibrations.GetValue());

    if (!Settings::IsConfiguringGlobal()) {
        ui->checkBoxAccurateVibration->setDisabled(true);
    }

    // Two rumble worlds with a selector between them: the accurate-vibration filter only
    // ever shaped the regular path, so it lives visibly inside the regular side where its
    // effect is, and nothing on the HD side claims otherwise.
    auto* rumble_tabs = new QTabWidget;
    auto* regular_page = new QWidget;
    auto* regular_col = new QVBoxLayout(regular_page);
    regular_col->addWidget(ui->vibrationStrengthGroup);
    regular_col->addWidget(ui->checkBoxAccurateVibration);
    regular_col->addStretch();
    auto* hd_page = new QWidget;
    auto* hd_col = new QVBoxLayout(hd_page);
    hd_col->setSpacing(6);
    rumble_tabs->addTab(regular_page, tr("Regular rumble"));
    rumble_tabs->addTab(hd_page, tr("HD rumble"));
    ui->verticalLayout->addWidget(rumble_tabs);
    ui->verticalLayout->addStretch();

    auto* hd_rumble_button = new QPushButton(tr("HD Rumble..."));
    hd_rumble_button->setToolTip(
        tr("HD Rumble for Pro Controller / Joy-Con and DualSense haptics (RenHDrum)."));
    connect(hd_rumble_button, &QPushButton::clicked, this, [this] {
        ConfigureHdRumble dialog(hid_core, this);
        if (dialog.exec() == QDialog::Accepted) {
            dialog.ApplyConfiguration();
        }
    });
    hd_col->addWidget(hd_rumble_button);
    hd_rumble_button->setVisible(InputCommon::IsRenHDrumBuilt());

    auto* reset_warning_button = new QPushButton(tr("Reset RenHDrum warning"));
    reset_warning_button->setToolTip(tr(
        "The no-controller warning counts down 10 s the first time it is acknowledged and "
        "5 s the second; resetting brings the long countdown back."));
    reset_warning_button->setEnabled(
        Settings::values.hd_rumble_warn_count.GetValue() != 0);
    connect(reset_warning_button, &QPushButton::clicked, this, [reset_warning_button] {
        Settings::values.hd_rumble_warn_count.SetValue(0);
        reset_warning_button->setEnabled(false);
    });
    hd_col->addWidget(reset_warning_button);
    reset_warning_button->setVisible(InputCommon::IsRenHDrumBuilt());

    // The 15 ms packet filter above is bypassed while HD Rumble is on (renhdrum_pad paces the
    // packets itself), so say so where the checkbox is, not only in the HD Rumble dialog.
    if (InputCommon::IsRenHDrumBuilt()) {
        ui->checkBoxAccurateVibration->setToolTip(
            tr("Not used while HD Rumble is on: the HD Rumble engine paces packets itself.\n"
               "Otherwise, off drops vibrations that arrive less than 15 ms apart."));
        auto* hd_rumble_note = new QLabel(tr(
            "HD Rumble paces packets itself, so \"Enable Accurate Vibration\" is bypassed while "
            "HD Rumble is on. That filter and its checkbox live on the Regular rumble side."));
        hd_rumble_note->setWordWrap(true);
        hd_col->addWidget(hd_rumble_note);
    }

    RetranslateUI();
}

ConfigureVibration::~ConfigureVibration() {
    StopVibrations();

    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        auto controller = hid_core.GetEmulatedControllerByIndex(i);
        controller->DeleteCallback(controller_callback_key[i]);
    }
};

void ConfigureVibration::ApplyConfiguration() {
    auto& players = Settings::values.players.GetValue();

    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        players[i].vibration_enabled = vibration_groupboxes[i]->isChecked();
        players[i].vibration_strength = vibration_spinboxes[i]->value();
    }

    Settings::values.enable_accurate_vibrations.SetValue(
        ui->checkBoxAccurateVibration->isChecked());
}

void ConfigureVibration::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureVibration::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureVibration::VibrateController(Core::HID::ControllerTriggerType type,
                                           std::size_t player_index) {
    if (type != Core::HID::ControllerTriggerType::Button) {
        return;
    }

    auto& player = Settings::values.players.GetValue()[player_index];
    auto controller = hid_core.GetEmulatedControllerByIndex(player_index);
    const int vibration_strength = vibration_spinboxes[player_index]->value();
    const auto& buttons = controller->GetButtonsValues();

    bool button_is_pressed = false;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i].value) {
            button_is_pressed = true;
            break;
        }
    }

    if (!button_is_pressed) {
        StopVibrations();
        return;
    }

    const bool old_vibration_enabled = player.vibration_enabled;
    const int old_vibration_strength = player.vibration_strength;
    player.vibration_enabled = true;
    player.vibration_strength = vibration_strength;

    const Core::HID::VibrationValue vibration{
        .low_amplitude = 1.0f,
        .low_frequency = 160.0f,
        .high_amplitude = 1.0f,
        .high_frequency = 320.0f,
    };
    controller->SetVibration(Core::HID::DeviceIndex::Left, vibration);
    controller->SetVibration(Core::HID::DeviceIndex::Right, vibration);

    // Restore previous values
    player.vibration_enabled = old_vibration_enabled;
    player.vibration_strength = old_vibration_strength;
}

void ConfigureVibration::StopVibrations() {
    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        auto controller = hid_core.GetEmulatedControllerByIndex(i);
        controller->SetVibration(Core::HID::DeviceIndex::Left, Core::HID::DEFAULT_VIBRATION_VALUE);
        controller->SetVibration(Core::HID::DeviceIndex::Right, Core::HID::DEFAULT_VIBRATION_VALUE);
    }
}
