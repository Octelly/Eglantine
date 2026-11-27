// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace Core::HID {
class HIDCore;
} // namespace Core::HID

namespace InputCommon {
struct RenHDrumOption;
} // namespace InputCommon

/// Settings for RenHDrum: HD Rumble on Switch Pro Controller / Joy-Con, DualSense haptics and
/// Xbox rumble. The rows come from the engine's own settings table (see
/// InputCommon::GetRenHDrumOptions), translated to native Qt widgets and bound to the same
/// Settings::values entries every other input setting uses; the SDL driver re-reads them on
/// every vibration, so connected controllers pick up a change once the dialog is accepted.
///
/// The layout follows the upstream RenHDrum panel: one window, the connected controller's
/// card first, the other controllers and the advanced rows behind collapsible sections.
class ConfigureHdRumble : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureHdRumble(Core::HID::HIDCore& hid_core_, QWidget* parent);
    ~ConfigureHdRumble() override;

    void ApplyConfiguration();

private:
    /// One row of the options table, bound to an Eglantine setting. The table index is what
    /// other rows' requires_row points at.
    struct OptionRow {
        std::string key;
        int table_index{-1};
        int def{};
        QCheckBox* check{};
        QSlider* slider{};
        QSpinBox* spin{};
        QComboBox* combo{};
        QWidget* container{};
    };

    /// One card per controller family: `device` is the bit RenHDrum's table marks rows with
    /// (1 Switch, 2 DualSense, 4 Xbox). A connected controller gets the status pill.
    QWidget* CreateDeviceCard(unsigned device, bool connected);
    /// The tier-1 rows of one controller family, behind the Advanced disclosure at the
    /// bottom of that family's card.
    QWidget* CreateAdvancedRows(unsigned device);
    /// One row in the upstream anatomy: label with muted help on the left, control
    /// right-aligned and vertically centered, appended to `rows` for the binding layer.
    void AddRow(QVBoxLayout* col, bool& first, const InputCommon::RenHDrumOption& opt,
                int table_index, int def);
    /// Plays a short HD Rumble tech demo on every player's pad -- soft/hard, long, deep,
    /// shrill, a climbing sweep, an echoed thump: RenHDrum renders every axis on a pad it
    /// drives, and whichever controllers are in reach answer, so a user can both verify the
    /// pipeline and feel which pad is which.
    void TestRumble();
    /// The no-controller warning: once per launch, with an escalating countdown on its only
    /// button (10 s, then 5 s, then none, tracked by hd_rumble_warn_count).
    void ShowNoControllerWarning();
    void LoadConfiguration();
    void RestoreDefaults();
    void UpdateEnabledState();

    Core::HID::HIDCore& hid_core;
    QCheckBox* hd_enabled{};
    QWidget* body{};
    /// Each Test rumble click gets a new one; a stale timed stop from an earlier click must
    /// not cut a newer pulse short.
    int test_generation{};

    /// Every option row of every card, in build order; the table index carries requires_row.
    std::vector<OptionRow> rows;

    QLabel* xbox_no_triggers{};
    int xbox_triggers_state{-1};
};
