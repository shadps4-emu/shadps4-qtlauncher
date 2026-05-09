// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QWheelEvent>
#include <SDL3/SDL_events.h>

#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "input/input.h"
#include "kbm_config_dialog.h"
#include "kbm_gui.h"
#include "kbm_help_dialog.h"
#include "ui_kbm_gui.h"

HelpDialog* HelpWindow;
KBMSettings::KBMSettings(std::shared_ptr<GameInfoClass> game_info_get,
                         std::shared_ptr<IpcClient> ipc_client, bool isGameRunning,
                         std::string GameRunningSerial, QWidget* parent)
    : QDialog(parent), m_game_info(game_info_get), m_ipc_client(ipc_client),
      GameRunning(isGameRunning), RunningGameSerial(GameRunningSerial), ui(new Ui::KBMSettings) {

    ui->setupUi(this);
    ui->PerGameCheckBox->setChecked(!EmulatorSettings.IsUseUnifiedInputConfig());
    ui->TextEditorButton->setFocus();
    this->setFocusPolicy(Qt::StrongFocus);

    ui->MouseJoystickBox->addItem("none");
    ui->MouseJoystickBox->addItem("right");
    ui->MouseJoystickBox->addItem("left");

    ui->ProfileComboBox->addItem(tr("Common Config"));
    for (int i = 0; i < m_game_info->m_games.size(); i++) {
        ui->ProfileComboBox->addItem(QString::fromStdString(m_game_info->m_games[i].serial));
    }

    ButtonsList = {
        {ui->CrossButton, "cross"},
        {ui->CrossButton2, "cross"},
        {ui->CrossButton3, "cross"},
        {ui->CircleButton, "circle"},
        {ui->CircleButton2, "circle"},
        {ui->CircleButton3, "circle"},
        {ui->TriangleButton, "triangle"},
        {ui->TriangleButton2, "triangle"},
        {ui->TriangleButton3, "triangle"},
        {ui->SquareButton, "square"},
        {ui->SquareButton2, "square"},
        {ui->SquareButton3, "square"},
        {ui->L1Button, "l1"},
        {ui->L1Button2, "l1"},
        {ui->L1Button3, "l1"},
        {ui->R1Button, "r1"},
        {ui->R1Button2, "r1"},
        {ui->R1Button3, "r1"},
        {ui->L2Button, "l2"},
        {ui->L2Button2, "l2"},
        {ui->L2Button3, "l2"},
        {ui->R2Button, "r2"},
        {ui->R2Button2, "r2"},
        {ui->R2Button3, "r2"},
        {ui->L3Button, "l3"},
        {ui->L3Button2, "l3"},
        {ui->L3Button3, "l3"},
        {ui->R3Button, "r3"},
        {ui->R3Button2, "r3"},
        {ui->R3Button3, "r3"},
        {ui->OptionsButton, "options"},
        {ui->OptionsButton2, "options"},
        {ui->OptionsButton3, "options"},
        {ui->TouchpadLeftButton, "touchpad_left"},
        {ui->TouchpadLeftButton2, "touchpad_left"},
        {ui->TouchpadLeftButton3, "touchpad_left"},
        {ui->TouchpadCenterButton, "touchpad_center"},
        {ui->TouchpadCenterButton2, "touchpad_center"},
        {ui->TouchpadCenterButton3, "touchpad_center"},
        {ui->TouchpadRightButton, "touchpad_right"},
        {ui->TouchpadRightButton2, "touchpad_right"},
        {ui->TouchpadRightButton3, "touchpad_right"},
        {ui->DpadUpButton, "pad_up"},
        {ui->DpadUpButton2, "pad_up"},
        {ui->DpadUpButton3, "pad_up"},
        {ui->DpadDownButton, "pad_down"},
        {ui->DpadDownButton2, "pad_down"},
        {ui->DpadDownButton3, "pad_down"},
        {ui->DpadLeftButton, "pad_left"},
        {ui->DpadLeftButton2, "pad_left"},
        {ui->DpadLeftButton3, "pad_left"},
        {ui->DpadRightButton, "pad_right"},
        {ui->DpadRightButton2, "pad_right"},
        {ui->DpadRightButton3, "pad_right"},
        {ui->LStickUpButton, "axis_left_y_minus"},
        {ui->LStickUpButton2, "axis_left_y_minus"},
        {ui->LStickUpButton3, "axis_left_y_minus"},
        {ui->LStickDownButton, "axis_left_y_plus"},
        {ui->LStickDownButton2, "axis_left_y_plus"},
        {ui->LStickDownButton3, "axis_left_y_plus"},
        {ui->LStickLeftButton, "axis_left_x_minus"},
        {ui->LStickLeftButton2, "axis_left_x_minus"},
        {ui->LStickLeftButton3, "axis_left_x_minus"},
        {ui->LStickRightButton, "axis_left_x_plus"},
        {ui->LStickRightButton2, "axis_left_x_plus"},
        {ui->LStickRightButton3, "axis_left_x_plus"},
        {ui->RStickUpButton, "axis_right_y_minus"},
        {ui->RStickUpButton2, "axis_right_y_minus"},
        {ui->RStickUpButton3, "axis_right_y_minus"},
        {ui->RStickDownButton, "axis_right_y_plus"},
        {ui->RStickDownButton2, "axis_right_y_plus"},
        {ui->RStickDownButton3, "axis_right_y_plus"},
        {ui->RStickLeftButton, "axis_right_x_minus"},
        {ui->RStickLeftButton2, "axis_right_x_minus"},
        {ui->RStickLeftButton3, "axis_right_x_minus"},
        {ui->RStickRightButton, "axis_right_x_plus"},
        {ui->RStickRightButton2, "axis_right_x_plus"},
        {ui->RStickRightButton3, "axis_right_x_plus"},
        {ui->LHalfButton, "leftjoystick_halfmode"},
        {ui->LHalfButton2, "leftjoystick_halfmode"},
        {ui->LHalfButton3, "leftjoystick_halfmode"},
        {ui->RHalfButton, "rightjoystick_halfmode"},
        {ui->RHalfButton2, "rightjoystick_halfmode"},
        {ui->RHalfButton3, "rightjoystick_halfmode"},
    };

    ButtonConnects();
    SetUIValuestoMappings("default");
    qApp->installEventFilter(this);

    ui->ProfileComboBox->setCurrentText(tr("Common Config"));
    ui->TitleLabel->setText(tr("Common Config"));
    config_id = "default";

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        if (button == ui->buttonBox->button(QDialogButtonBox::Save)) {
            if (HelpWindowOpen) {
                HelpWindow->close();
                HelpWindowOpen = false;
            }
            SaveKBMConfig(true);
        } else if (button == ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)) {
            SetDefault();
        } else if (button == ui->buttonBox->button(QDialogButtonBox::Apply)) {
            SaveKBMConfig(false);
        }
    });

    ui->buttonBox->button(QDialogButtonBox::Save)->setText(tr("Save"));
    ui->buttonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));
    ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Defaults"));
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));

    connect(ui->HelpButton, &QPushButton::clicked, this, &KBMSettings::onHelpClicked);
    connect(ui->TextEditorButton, &QPushButton::clicked, this, [this]() {
        auto kbmWindow = new EditorDialog(this);
        connect(kbmWindow, &EditorDialog::configSaved, this, &QWidget::close);
        kbmWindow->exec();
        SetUIValuestoMappings(config_id);
    });

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [this] {
        QWidget::close();
        if (HelpWindowOpen) {
            HelpWindow->close();
            HelpWindowOpen = false;
        }
    });

    connect(ui->ProfileComboBox, &QComboBox::currentTextChanged, this, [this] {
        GetGameTitle();
        SetUIValuestoMappings(config_id);
    });

    connect(ui->CopyCommonButton, &QPushButton::clicked, this, [this] {
        if (ui->ProfileComboBox->currentText() == tr("Common Config")) {
            QMessageBox::information(this, tr("Common Config Selected"),
                                     // clang-format off
tr("This button copies mappings from the Common Config to the currently selected profile, and cannot be used when the currently selected profile is the Common Config."));
            // clang-format on
        } else {
            QMessageBox::StandardButton reply =
                QMessageBox::question(this, tr("Copy values from Common Config"),
                                      // clang-format off
tr("Do you want to overwrite existing mappings with the mappings from the Common Config?"),
                                      // clang-format on
                                      QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                SetUIValuestoMappings("default");
            }
        }
    });

    connect(ui->DeadzoneOffsetSlider, &QSlider::valueChanged, this, [this](int value) {
        QString DOSValue = QString::number(value / 100.0, 'f', 2);
        ui->DeadzoneOffsetLabel->setText(DOSValue);
    });

    connect(ui->SpeedMultiplierSlider, &QSlider::valueChanged, this, [this](int value) {
        QString SMSValue = QString::number(value / 10.0, 'f', 1);
        ui->SpeedMultiplierLabel->setText(SMSValue);
    });

    connect(ui->SpeedOffsetSlider, &QSlider::valueChanged, this, [this](int value) {
        QString SOSValue = QString::number(value / 1000.0, 'f', 3);
        ui->SpeedOffsetLabel->setText(SOSValue);
    });

    connect(this, &KBMSettings::PushKBMEvent, this, [this]() { CheckMapping(MappingButton); });
}

void KBMSettings::ButtonConnects() {
    for (auto& entry : ButtonsList) {
        connect(entry.first, &QPushButton::clicked, this,
                [this, &entry]() { StartTimer(entry.first); });

        connect(entry.first, &QRightClickButton::rightClicked, this,
                [this, &entry]() { entry.first->setText("unmapped"); });
    }
}

void KBMSettings::DisableMappingButtons() {
    for (auto& entry : ButtonsList) {
        entry.first->setEnabled(false);
    }

    ui->scrollArea->verticalScrollBar()->setEnabled(false);

    ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(false);
    ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setEnabled(false);
}

void KBMSettings::EnableMappingButtons() {
    for (auto& entry : ButtonsList) {
        entry.first->setEnabled(true);
    }

    ui->scrollArea->verticalScrollBar()->setEnabled(true);

    ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
    ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setEnabled(true);
}

void KBMSettings::SaveKBMConfig(bool close_on_save) {
    std::string output_string = "", input_string = "";
    std::vector<std::string> lines, inputs;

    // Comment lines for config file
    lines.push_back("#Feeling lost? Check out the Help section!");
    lines.push_back("");
    lines.push_back("#Keyboard bindings");
    lines.push_back("");

    for (const auto& entry : ButtonsList) {
        input_string = entry.first->text().toStdString();
        output_string = entry.second;
        if (input_string != "unmapped") {
            lines.push_back(output_string + " = " + input_string);
            inputs.push_back(input_string);
        }
    }

    lines.push_back("");
    lines.push_back("mouse_to_joystick = " + ui->MouseJoystickBox->currentText().toStdString());

    std::string DOString = std::format("{:.2f}", (ui->DeadzoneOffsetSlider->value() / 100.f));
    std::string SMString = std::format("{:.1f}", (ui->SpeedMultiplierSlider->value() / 10.f));
    std::string SOString = std::format("{:.3f}", (ui->SpeedOffsetSlider->value() / 1000.f));
    input_string = DOString + ", " + SMString + ", " + SOString;
    output_string = "mouse_movement_params";
    lines.push_back(output_string + " = " + input_string);

    lines.push_back("");
    const auto config_file = Input::GetFoolproofInputConfigFile(config_id);
    std::fstream file(config_file);
    int lineCount = 0;
    std::string line;
    while (std::getline(file, line)) {
        lineCount++;

        if (line.empty()) {
            lines.push_back(line);
            continue;
        }

        std::size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            if (!line.contains("Keyboard bindings") && !line.contains("Feeling lost") &&
                !line.contains("Alternatives for users"))
                lines.push_back(line);
            continue;
        }

        std::size_t equal_pos = line.find('=');
        if (equal_pos == std::string::npos) {
            lines.push_back(line);
            continue;
        }

        output_string = line.substr(0, equal_pos - 1);
        input_string = line.substr(equal_pos + 2);

        bool controllerInputdetected = false;
        for (const std::string& input : ControllerInputs) {
            // Needed to avoid detecting backspace while detecting back
            if (input_string.contains(input) && !input_string.contains("backspace")) {
                controllerInputdetected = true;
                break;
            }
        }

        if (controllerInputdetected || output_string == "analog_deadzone" ||
            output_string == "override_controller_color" || output_string.contains("hotkey")) {
            lines.push_back(line);
        }
    }
    file.close();

    // Prevent duplicate inputs for KBM as this breaks the engine
    bool duplicateFound = false;
    QSet<QString> duplicateMappings;

    for (auto it = inputs.begin(); it != inputs.end(); ++it) {
        if (std::find(it + 1, inputs.end(), *it) != inputs.end()) {
            duplicateFound = true;
            duplicateMappings.insert(QString::fromStdString(*it));
        }
    }

    if (duplicateFound) {
        QStringList duplicatesList;
        for (const QString& mapping : duplicateMappings) {
            for (const auto& entry : ButtonsList) {
                if (entry.first->text() == mapping)
                    duplicatesList.append(entry.first->objectName() + " - " + mapping);
            }
        }
        QMessageBox::information(
            this, tr("Unable to Save"),
            // clang-format off
QString(tr("Cannot bind any unique input more than once. Duplicate inputs mapped to the following buttons:\n\n%1").arg(duplicatesList.join("\n"))));
        // clang-format on
        return;
    }

    std::vector<std::string> save;
    bool CurrentLineEmpty = false, LastLineEmpty = false;
    for (auto const& line : lines) {
        LastLineEmpty = CurrentLineEmpty ? true : false;
        CurrentLineEmpty = line.empty() ? true : false;
        if (!CurrentLineEmpty || !LastLineEmpty)
            save.push_back(line);
    }

    std::ofstream output_file(config_file);
    for (auto const& line : save) {
        output_file << line << '\n';
    }
    output_file.close();

    EmulatorSettings.SetUseUnifiedInputConfig(!ui->PerGameCheckBox->isChecked());
    EmulatorSettings.Save();

    if (GameRunning) {
        EmulatorSettings.IsUseUnifiedInputConfig() ? m_ipc_client->reloadInputs("default")
                                                   : m_ipc_client->reloadInputs(RunningGameSerial);
    }

    if (close_on_save)
        QWidget::close();
}

void KBMSettings::SetDefault() {
    ui->CrossButton->setText("kp2");
    ui->CircleButton->setText("kp6");
    ui->TriangleButton->setText("kp8");
    ui->SquareButton->setText("kp4");

    ui->L1Button->setText("q");
    ui->L2Button->setText("e");
    ui->L3Button->setText("x");
    ui->R1Button->setText("u");
    ui->R2Button->setText("o");
    ui->R3Button->setText("m");

    ui->TouchpadLeftButton->setText("space");
    ui->TouchpadCenterButton->setText("unmapped");
    ui->TouchpadRightButton->setText("unmapped");
    ui->OptionsButton->setText("enter");

    ui->DpadUpButton->setText("up");
    ui->DpadDownButton->setText("down");
    ui->DpadLeftButton->setText("left");
    ui->DpadRightButton->setText("right");

    ui->LStickUpButton->setText("w");
    ui->LStickDownButton->setText("s");
    ui->LStickLeftButton->setText("a");
    ui->LStickRightButton->setText("d");

    ui->RStickUpButton->setText("i");
    ui->RStickDownButton->setText("k");
    ui->RStickLeftButton->setText("j");
    ui->RStickRightButton->setText("l");

    ui->LHalfButton->setText("unmapped");
    ui->RHalfButton->setText("unmapped");

    ui->MouseJoystickBox->setCurrentText("none");
    ui->DeadzoneOffsetSlider->setValue(50);
    ui->SpeedMultiplierSlider->setValue(10);
    ui->SpeedOffsetSlider->setValue(125);
}

void KBMSettings::SetUIValuestoMappings(std::string config_id) {
    for (auto& entry : ButtonsList) {
        entry.first->setText("unmapped");
    }

    const auto config_file = Input::GetFoolproofInputConfigFile(config_id);
    std::ifstream file(config_file);

    int lineCount = 0;
    std::string line = "";
    while (std::getline(file, line)) {
        lineCount++;

        std::size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos)
            line = line.substr(0, comment_pos);

        std::size_t equal_pos = line.find('=');
        if (equal_pos == std::string::npos)
            continue;

        std::string output_string = line.substr(0, equal_pos - 1);
        std::string input_string = line.substr(equal_pos + 2);

        bool controllerInputdetected = false;
        for (const std::string& input : ControllerInputs) {
            // Needed to avoid detecting backspace while detecting back
            if (input_string.contains(input) && !input_string.contains("backspace")) {
                controllerInputdetected = true;
                break;
            }
        }

        if (!controllerInputdetected) {
            for (auto& entry : ButtonsList) {
                if (output_string == entry.second && entry.first->text() == "unmapped") {
                    entry.first->setText(QString::fromStdString(input_string));
                    break;
                }
            }

            if (output_string.contains("mouse_movement_params")) {
                std::size_t comma_pos = line.find(',');
                if (comma_pos != std::string::npos) {
                    const std::string old_locale = std::setlocale(LC_NUMERIC, nullptr);
                    // Set locale to use a dot as a decimal separator
                    std::setlocale(LC_NUMERIC, "C");
                    std::string DOstring = line.substr(equal_pos + 1, comma_pos - (equal_pos + 1));
                    float DOffsetValue = std::stof(DOstring) * 100.f;
                    int DOffsetInt = static_cast<int>(DOffsetValue);
                    ui->DeadzoneOffsetSlider->setValue(DOffsetInt);
                    QString LabelValue = QString::number(DOffsetInt / 100.0, 'f', 2);
                    ui->DeadzoneOffsetLabel->setText(LabelValue);
                    std::string SMSOstring = line.substr(comma_pos + 1);
                    std::size_t comma_pos2 = SMSOstring.find(',');
                    if (comma_pos2 != std::string::npos) {
                        std::string SMstring = SMSOstring.substr(0, comma_pos2);
                        float SpeedMultValue = std::clamp(std::stof(SMstring) * 10.0f, 1.0f, 50.0f);
                        int SpeedMultInt = static_cast<int>(SpeedMultValue);
                        ui->SpeedMultiplierSlider->setValue(SpeedMultInt);
                        LabelValue = QString::number(SpeedMultInt / 10.0, 'f', 1);
                        ui->SpeedMultiplierLabel->setText(LabelValue);

                        std::string SOstring = SMSOstring.substr(comma_pos2 + 1);
                        float SOffsetValue = std::stof(SOstring) * 1000.0;
                        int SOffsetInt = static_cast<int>(SOffsetValue);
                        ui->SpeedOffsetSlider->setValue(SOffsetInt);
                        LabelValue = QString::number(SOffsetInt / 1000.0, 'f', 3);
                        ui->SpeedOffsetLabel->setText(LabelValue);
                    }
                    // Restore locale
                    std::setlocale(LC_NUMERIC, old_locale.c_str());
                }
            } else if (output_string == "mouse_to_joystick") {
                ui->MouseJoystickBox->setCurrentText(QString::fromStdString(input_string));
            }
        }
    }
    file.close();
}

void KBMSettings::GetGameTitle() {
    if (ui->ProfileComboBox->currentText() == tr("Common Config")) {
        ui->TitleLabel->setText(tr("Common Config"));
    } else {
        for (int i = 0; i < m_game_info->m_games.size(); i++) {
            if (m_game_info->m_games[i].serial ==
                ui->ProfileComboBox->currentText().toStdString()) {
                ui->TitleLabel->setText(QString::fromStdString(m_game_info->m_games[i].name));
            }
        }
    }
    config_id = (ui->ProfileComboBox->currentText() == tr("Common Config"))
                    ? "default"
                    : ui->ProfileComboBox->currentText().toStdString();
}

void KBMSettings::onHelpClicked() {
    if (!HelpWindowOpen) {
        HelpWindow = new HelpDialog(&HelpWindowOpen, this);
        HelpWindow->setWindowTitle(tr("Help"));
        HelpWindow->setAttribute(Qt::WA_DeleteOnClose); // Clean up on close
        HelpWindow->show();
        HelpWindowOpen = true;
    } else {
        HelpWindow->close();
        HelpWindowOpen = false;
    }
}

void KBMSettings::StartTimer(QRightClickButton*& button) {
    MappingTimer = 3;
    EnableMapping = true;
    MappingCompleted = false;
    mapping = button->text();

    DisableMappingButtons();
    button->setText(tr("Press a key") + " [" + QString::number(MappingTimer) + "]");
    timer = new QTimer(this);
    MappingButton = button;
    connect(timer, &QTimer::timeout, this, [this]() { CheckMapping(MappingButton); });
    timer->start(1000);
    button->setStyleSheet("border: 1px solid lightblue;");
}

void KBMSettings::CheckMapping(QRightClickButton*& button) {
    MappingTimer -= 1;
    button->setText(tr("Press a key") + " [" + QString::number(MappingTimer) + "]");

    if (pressedKeys.size() > 0) {
        QStringList keyStrings;

        for (const QString& buttonAction : pressedKeys) {
            keyStrings << buttonAction;
        }

        QString combo = keyStrings.join(",");
        SetMapping(combo);
        MappingCompleted = true;
        EnableMapping = false;

        MappingButton->setText(combo);
        pressedKeys.clear();
        timer->stop();
    }
    if (MappingCompleted) {
        button->setText(mapping);
        button->setStyleSheet("");

        EnableMapping = false;
        EnableMappingButtons();
        timer->stop();
    }

    if (MappingTimer <= 0) {
        button->setText(mapping);
        button->setStyleSheet("");

        EnableMapping = false;
        EnableMappingButtons();
        timer->stop();
    }
}

void KBMSettings::SetMapping(QString input) {
    mapping = input;
    MappingCompleted = true;
}

// Helper lambda to get the modified button text based on the current keyboard modifiers
auto GetModifiedButton = [](Qt::KeyboardModifiers modifier, const std::string& m_button,
                            const std::string& n_button) -> QString {
    if (QApplication::keyboardModifiers() & modifier) {
        return QString::fromStdString(m_button);
    } else {
        return QString::fromStdString(n_button);
    }
};

bool KBMSettings::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Close) {
        if (HelpWindowOpen) {
            HelpWindow->close();
            HelpWindowOpen = false;
        }
    }

    if (EnableMapping) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->isAutoRepeat())
                return true;

            if (pressedKeys.size() >= 3) {
                return true;
            }

            switch (keyEvent->key()) {
            // modifiers
            case Qt::Key_Shift:
                keyEvent->nativeScanCode() == LSHIFT_KEY ? pressedKeys.insert(1, "lshift")
                                                         : pressedKeys.insert(2, "rshift");
                break;
            case Qt::Key_Alt:
                keyEvent->nativeScanCode() == LALT_KEY ? pressedKeys.insert(3, "lalt")
                                                       : pressedKeys.insert(4, "ralt");
                break;
            case Qt::Key_Control:
                keyEvent->nativeScanCode() == LCTRL_KEY ? pressedKeys.insert(5, "lctrl")
                                                        : pressedKeys.insert(6, "rctrl");
                break;
            case Qt::Key_Meta:
#ifdef _WIN32
                pressedKeys.insert(7, "lwin");
#else
                pressedKeys.insert(7, "lmeta");
#endif
                break;

                // alphanumeric
            case Qt::Key_A:
                pressedKeys.insert(20, "a");
                break;
            case Qt::Key_B:
                pressedKeys.insert(21, "b");
                break;
            case Qt::Key_C:
                pressedKeys.insert(22, "c");
                break;
            case Qt::Key_D:
                pressedKeys.insert(23, "d");
                break;
            case Qt::Key_E:
                pressedKeys.insert(24, "e");
                break;
            case Qt::Key_F:
                pressedKeys.insert(25, "f");
                break;
            case Qt::Key_G:
                pressedKeys.insert(26, "g");
                break;
            case Qt::Key_H:
                pressedKeys.insert(27, "h");
                break;
            case Qt::Key_I:
                pressedKeys.insert(28, "i");
                break;
            case Qt::Key_J:
                pressedKeys.insert(29, "j");
                break;
            case Qt::Key_K:
                pressedKeys.insert(30, "k");
                break;
            case Qt::Key_L:
                pressedKeys.insert(31, "l");
                break;
            case Qt::Key_M:
                pressedKeys.insert(32, "m");
                break;
            case Qt::Key_N:
                pressedKeys.insert(33, "n");
                break;
            case Qt::Key_O:
                pressedKeys.insert(34, "o");
                break;
            case Qt::Key_P:
                pressedKeys.insert(35, "p");
                break;
            case Qt::Key_Q:
                pressedKeys.insert(36, "q");
                break;
            case Qt::Key_R:
                pressedKeys.insert(37, "r");
                break;
            case Qt::Key_S:
                pressedKeys.insert(38, "s");
                break;
            case Qt::Key_T:
                pressedKeys.insert(39, "t");
                break;
            case Qt::Key_U:
                pressedKeys.insert(40, "u");
                break;
            case Qt::Key_V:
                pressedKeys.insert(41, "v");
                break;
            case Qt::Key_W:
                pressedKeys.insert(42, "w");
                break;
            case Qt::Key_X:
                pressedKeys.insert(43, "x");
                break;
            case Qt::Key_Y:
                pressedKeys.insert(44, "y");
                break;
            case Qt::Key_Z:
                pressedKeys.insert(45, "z");
                break;
            case Qt::Key_0:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(46, "kp0")
                    : pressedKeys.insert(56, "0");
                break;
            case Qt::Key_1:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(47, "kp1")
                    : pressedKeys.insert(57, "1");
                break;
            case Qt::Key_2:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(48, "kp2")
                    : pressedKeys.insert(58, "2");
                break;
            case Qt::Key_3:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(49, "kp3")
                    : pressedKeys.insert(59, "3");
                break;
            case Qt::Key_4:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(50, "kp4")
                    : pressedKeys.insert(60, "4");
                break;
            case Qt::Key_5:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(51, "kp5")
                    : pressedKeys.insert(61, "5");
                break;
            case Qt::Key_6:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(52, "kp6")
                    : pressedKeys.insert(62, "6");
                break;
            case Qt::Key_7:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(53, "kp7")
                    : pressedKeys.insert(63, "7");
                break;
            case Qt::Key_8:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(54, "kp8")
                    : pressedKeys.insert(64, "8");
                break;
            case Qt::Key_9:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(55, "kp9")
                    : pressedKeys.insert(65, "9");
                break;

                // symbols
            case Qt::Key_Asterisk:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(66, "kpasterisk")
                    : pressedKeys.insert(74, "asterisk");
                break;
            case Qt::Key_Minus:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(67, "kpminus")
                    : pressedKeys.insert(75, "minus");
                break;
            case Qt::Key_Equal:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(68, "kpequals")
                    : pressedKeys.insert(76, "equals");
                break;
            case Qt::Key_Plus:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(69, "kpplus")
                    : pressedKeys.insert(77, "plus");
                break;
            case Qt::Key_Slash:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(70, "kpslash")
                    : pressedKeys.insert(78, "slash");
                break;
            case Qt::Key_Comma:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(71, "kpcomma")
                    : pressedKeys.insert(79, "comma");
                break;
            case Qt::Key_Period:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(72, "kpperiod")
                    : pressedKeys.insert(80, "period");
                break;
            case Qt::Key_Enter:
                QApplication::keyboardModifiers() & Qt::KeypadModifier
                    ? pressedKeys.insert(73, "kpenter")
                    : pressedKeys.insert(81, "enter");
                break;
            case Qt::Key_QuoteLeft:
                pressedKeys.insert(82, "grave");
                break;
            case Qt::Key_AsciiTilde:
                pressedKeys.insert(83, "tilde");
                break;
            case Qt::Key_Exclam:
                pressedKeys.insert(84, "exclamation");
                break;
            case Qt::Key_At:
                pressedKeys.insert(85, "at");
                break;
            case Qt::Key_NumberSign:
                pressedKeys.insert(86, "hash");
                break;
            case Qt::Key_Dollar:
                pressedKeys.insert(87, "dollar");
                break;
            case Qt::Key_Percent:
                pressedKeys.insert(88, "percent");
                break;
            case Qt::Key_AsciiCircum:
                pressedKeys.insert(89, "caret");
                break;
            case Qt::Key_Ampersand:
                pressedKeys.insert(90, "ampersand");
                break;
            case Qt::Key_ParenLeft:
                pressedKeys.insert(91, "lparen");
                break;
            case Qt::Key_ParenRight:
                pressedKeys.insert(92, "rparen");
                break;
            case Qt::Key_BracketLeft:
                pressedKeys.insert(93, "lbracket");
                break;
            case Qt::Key_BracketRight:
                pressedKeys.insert(94, "rbracket");
                break;
            case Qt::Key_BraceLeft:
                pressedKeys.insert(95, "lbrace");
                break;
            case Qt::Key_BraceRight:
                pressedKeys.insert(96, "rbrace");
                break;
            case Qt::Key_Underscore:
                pressedKeys.insert(97, "underscore");
                break;
            case Qt::Key_Backslash:
                pressedKeys.insert(98, "backslash");
                break;
            case Qt::Key_Bar:
                pressedKeys.insert(99, "pipe");
                break;
            case Qt::Key_Semicolon:
                pressedKeys.insert(100, "semicolon");
                break;
            case Qt::Key_Colon:
                pressedKeys.insert(101, "colon");
                break;
            case Qt::Key_Apostrophe:
                pressedKeys.insert(102, "apostrophe");
                break;
            case Qt::Key_QuoteDbl:
                pressedKeys.insert(103, "quote");
                break;
            case Qt::Key_Less:
                pressedKeys.insert(104, "less");
                break;
            case Qt::Key_Greater:
                pressedKeys.insert(105, "greater");
                break;
            case Qt::Key_Question:
                pressedKeys.insert(106, "question");
                break;

                // special keys
            case Qt::Key_Print:
                pressedKeys.insert(107, "printscreen");
                break;
            case Qt::Key_ScrollLock:
                pressedKeys.insert(108, "scrolllock");
                break;
            case Qt::Key_Pause:
                pressedKeys.insert(109, "pausebreak");
                break;
            case Qt::Key_Backspace:
                pressedKeys.insert(110, "backspace");
                break;
            case Qt::Key_Insert:
                pressedKeys.insert(111, "insert");
                break;
            case Qt::Key_Delete:
                pressedKeys.insert(112, "delete");
                break;
            case Qt::Key_Home:
                pressedKeys.insert(113, "home");
                break;
            case Qt::Key_End:
                pressedKeys.insert(114, "end");
                break;
            case Qt::Key_PageUp:
                pressedKeys.insert(115, "pgup");
                break;
            case Qt::Key_PageDown:
                pressedKeys.insert(116, "pgdown");
                break;
            case Qt::Key_Tab:
                pressedKeys.insert(117, "tab");
                break;
            case Qt::Key_CapsLock:
                pressedKeys.insert(118, "capslock");
                break;
            case Qt::Key_Return:
                pressedKeys.insert(119, "enter");
                break;
            case Qt::Key_Space:
                pressedKeys.insert(120, "space");
                break;
            case Qt::Key_Up:
                pressedKeys.insert(121, "up");
                break;
            case Qt::Key_Down:
                pressedKeys.insert(122, "down");
                break;
            case Qt::Key_Left:
                pressedKeys.insert(123, "left");
                break;
            case Qt::Key_Right:
                pressedKeys.insert(124, "right");
                break;

                // cancel mapping
            case Qt::Key_Escape:
                pressedKeys.insert(125, "escape");
                break;
            default:
                break;
            }
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (pressedKeys.size() < 3) {
                switch (mouseEvent->button()) {
                case Qt::LeftButton:
                    pressedKeys.insert(126, "leftbutton");
                    break;
                case Qt::RightButton:
                    pressedKeys.insert(128, "rightbutton");
                    break;
                case Qt::MiddleButton:
                    pressedKeys.insert(127, "middlebutton");
                    break;
                case Qt::XButton1:
                    pressedKeys.insert(129, "sidebuttonback");
                    break;
                case Qt::XButton2:
                    pressedKeys.insert(130, "sidebuttonforward");
                    break;

                    // default case
                default:
                    break;
                    // bottom text
                }
                return true;
            }
        }

        const QList<QPushButton*> AxisList = {
            ui->LStickUpButton, ui->LStickDownButton, ui->LStickLeftButton, ui->LStickRightButton,
            ui->RStickUpButton, ui->LStickDownButton, ui->LStickLeftButton, ui->RStickRightButton};

        if (event->type() == QEvent::Wheel) {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            if (pressedKeys.size() < 3) {
                if (wheelEvent->angleDelta().y() > 5) {
                    if (std::find(AxisList.begin(), AxisList.end(), MappingButton) ==
                        AxisList.end()) {
                        pressedKeys.insert(131, "mousewheelup");
                        if (QApplication::keyboardModifiers() == Qt::NoModifier)
                            emit PushKBMEvent();
                    } else {
                        QMessageBox::information(
                            this, tr("Cannot set mapping"),
                            tr("Mousewheel cannot be mapped to stick outputs"));
                    }
                } else if (wheelEvent->angleDelta().y() < -5) {
                    if (std::find(AxisList.begin(), AxisList.end(), MappingButton) ==
                        AxisList.end()) {
                        pressedKeys.insert(132, "mousewheeldown");
                        if (QApplication::keyboardModifiers() == Qt::NoModifier)
                            emit PushKBMEvent();
                    } else {
                        QMessageBox::information(
                            this, tr("Cannot set mapping"),
                            tr("Mousewheel cannot be mapped to stick outputs"));
                    }
                }
                if (wheelEvent->angleDelta().x() > 5) {
                    if (std::find(AxisList.begin(), AxisList.end(), MappingButton) ==
                        AxisList.end()) {
                        // QT changes scrolling to horizontal for all widgets with the alt modifier
                        QApplication::keyboardModifiers() & Qt::AltModifier
                            ? pressedKeys.insert(131, "mousewheelup")
                            : pressedKeys.insert(134, "mousewheelright");
                        if (QApplication::keyboardModifiers() == Qt::NoModifier)
                            emit PushKBMEvent();
                    } else {
                        QMessageBox::information(
                            this, tr("Cannot set mapping"),
                            tr("Mousewheel cannot be mapped to stick outputs"));
                    }
                } else if (wheelEvent->angleDelta().x() < -5) {
                    if (std::find(AxisList.begin(), AxisList.end(), MappingButton) ==
                        AxisList.end()) {
                        QApplication::keyboardModifiers() & Qt::AltModifier
                            ? pressedKeys.insert(132, "mousewheeldown")
                            : pressedKeys.insert(133, "mousewheelleft");
                        if (QApplication::keyboardModifiers() == Qt::NoModifier)
                            emit PushKBMEvent();
                    } else {
                        QMessageBox::information(
                            this, tr("Cannot set mapping"),
                            tr("Mousewheel cannot be mapped to stick outputs"));
                    }
                }
            }
        }

        if (event->type() == QEvent::KeyRelease || event->type() == QEvent::MouseButtonRelease)
            emit PushKBMEvent();
    }
    return QDialog::eventFilter(obj, event);
}

KBMSettings::~KBMSettings() {
    qApp->removeEventFilter(this);
}
