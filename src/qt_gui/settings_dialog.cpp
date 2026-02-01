// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>
#include <QCompleter>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFileDialog>
#include <QHoverEvent>
#include <QMessageBox>
#include <QTabWidget>
#include <QTextEdit>
#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <toml.hpp>

#include "common/config.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "qt_gui/compatibility_info.h"
#ifdef ENABLE_UPDATER
#include "check_update.h"
#endif
#include "background_music_player.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "core/emulator_state.h"
#include "log_presets_dialog.h"
#include "sdl_event_wrapper.h"
#include "settings_dialog.h"
#include "ui_settings_dialog.h"
// #include "video_core/renderer_vulkan/vk_instance.h"
// #include "video_core/renderer_vulkan/vk_presenter.h"

#define VOLK_IMPLEMENTATION
#include "volk.h"

// extern std::unique_ptr<Vulkan::Presenter> presenter;

QStringList languageNames = {"Arabic",
                             "Czech",
                             "Danish",
                             "Dutch",
                             "English (United Kingdom)",
                             "English (United States)",
                             "Finnish",
                             "French (Canada)",
                             "French (France)",
                             "German",
                             "Greek",
                             "Hungarian",
                             "Indonesian",
                             "Italian",
                             "Japanese",
                             "Korean",
                             "Norwegian (Bokmaal)",
                             "Polish",
                             "Portuguese (Brazil)",
                             "Portuguese (Portugal)",
                             "Romanian",
                             "Russian",
                             "Simplified Chinese",
                             "Spanish (Latin America)",
                             "Spanish (Spain)",
                             "Swedish",
                             "Thai",
                             "Traditional Chinese",
                             "Turkish",
                             "Ukrainian",
                             "Vietnamese"};

const QVector<int> languageIndexes = {21, 23, 14, 6, 18, 1, 12, 22, 2, 4,  25, 24, 29, 5,  0, 9,
                                      15, 16, 17, 7, 26, 8, 11, 20, 3, 13, 27, 10, 19, 30, 28};
QMap<QString, QString> channelMap;
QMap<QString, QString> logTypeMap;
QMap<QString, QString> screenModeMap;
QMap<QString, QString> presentModeMap;
QMap<QString, QString> chooseHomeTabMap;
QMap<QString, QString> micMap;

int backgroundImageOpacitySlider_backup;
int bgm_volume_backup;

static std::vector<QString> m_physical_devices;

SettingsDialog::SettingsDialog(std::shared_ptr<gui_settings> gui_settings,
                               std::shared_ptr<CompatibilityInfoClass> m_compat_info,
                               std::shared_ptr<IpcClient> ipc_client, QWidget* parent,
                               bool is_running, bool is_specific, std::string gsc_serial)
    : QDialog(parent), ui(new Ui::SettingsDialog), m_gui_settings(std::move(gui_settings)),
      m_ipc_client(ipc_client), is_game_running(is_running), is_game_specific(is_specific),
      gs_serial(gsc_serial) {

    ui->setupUi(this);
    close_target = this;
    ui->tabWidgetSettings->setUsesScrollButtons(false);
    getPhysicalDevices();

    initialHeight = this->height();

    // Add a small clear "x" button inside the Log Filter input
    ui->logFilterLineEdit->setClearButtonEnabled(true);

    if (is_game_specific) {
        // Paths tab
        ui->tabWidgetSettings->setTabVisible(5, false);
        ui->chooseHomeTabComboBox->removeItem(5);

        // Frontend tab
        ui->tabWidgetSettings->setTabVisible(1, false);
        ui->chooseHomeTabComboBox->removeItem(1);

    } else {
        // Experimental tab
        ui->tabWidgetSettings->setTabVisible(8, false);
        ui->chooseHomeTabComboBox->removeItem(8);
    }

    config_file =
        is_game_specific
            ? Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) / (gs_serial + ".toml")
            : Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "config.toml";

    ui->buttonBox->button(QDialogButtonBox::StandardButton::Close)->setFocus();

    logTypeMap = {{tr("async"), "async"}, {tr("sync"), "sync"}};
    screenModeMap = {{tr("Fullscreen (Borderless)"), "Fullscreen (Borderless)"},
                     {tr("Windowed"), "Windowed"},
                     {tr("Fullscreen"), "Fullscreen"}};
    presentModeMap = {{tr("Mailbox (Vsync)"), "Mailbox"},
                      {tr("Fifo (Vsync)"), "Fifo"},
                      {tr("Immediate (No Vsync)"), "Immediate"}};
    chooseHomeTabMap = {{tr("General"), "General"},
                        {tr("Frontend"), "Frontend"},
                        {tr("Graphics"), "Graphics"},
                        {tr("User"), "User"},
                        {tr("Input"), "Input"},
                        {tr("Paths"), "Paths"},
                        {tr("Log"), "Log"},
                        {tr("Debug"), "Debug"},
                        {tr("Experimental"), "Experimental"}};
    micMap = {{tr("None"), "None"}, {tr("Default Device"), "Default Device"}};

    if (m_physical_devices.empty()) {
        // Populate cache of physical devices.
        // Vulkan::Instance instance(false, false);
        /* auto physical_devices = getPhysicalDevices();
        for (const vk::PhysicalDevice physical_device : physical_devices) {
            auto prop = physical_device.getProperties();
            QString name = QString::fromUtf8(prop.deviceName, -1);
            if (prop.apiVersion < Vulkan::TargetVulkanApiVersion) {
                name += tr(" * Unsupported Vulkan Version");
            }
            m_physical_devices.push_back(name);
        }*/
        getPhysicalDevices();
    }

    // Add list of available GPUs
    ui->graphicsAdapterBox->addItem(tr("Auto Select")); // -1, auto selection
    for (const auto& device : m_physical_devices) {
        ui->graphicsAdapterBox->addItem(device);
    }

    ui->consoleLanguageComboBox->addItems(languageNames);

    QCompleter* completer = new QCompleter(languageNames, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->consoleLanguageComboBox->setCompleter(completer);

    ui->hideCursorComboBox->addItem(tr("Never"));
    ui->hideCursorComboBox->addItem(tr("Idle"));
    ui->hideCursorComboBox->addItem(tr("Always"));

    ui->micComboBox->addItem(micMap.key("None"), "None");
    ui->micComboBox->addItem(micMap.key("Default Device"), "Default Device");
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    int count = 0;
    SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&count);
    if (devices) {
        for (int i = 0; i < count; ++i) {
            SDL_AudioDeviceID devId = devices[i];
            const char* name = SDL_GetAudioDeviceName(devId);
            if (name) {
                QString qname = QString::fromUtf8(name);
                ui->micComboBox->addItem(qname, QString::number(devId));
            }
        }
        SDL_free(devices);
    } else {
        qDebug() << "Erro SDL_GetAudioRecordingDevices:" << SDL_GetError();
    }

    ui->usbComboBox->addItem(tr("Real USB Device"));
    ui->usbComboBox->addItem(tr("Skylander Portal"));
    ui->usbComboBox->addItem(tr("Infinity Base"));
    ui->usbComboBox->addItem(tr("Dimensions Toypad"));

    InitializeEmulatorLanguages();
    onAudioDeviceChange(true);
    LoadValuesFromConfig();

    defaultTextEdit = tr("Point your mouse at an option to display its description.");
    ui->descriptionText->setText(defaultTextEdit);

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [this]() { close_target->close(); });

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this,
            [this](QAbstractButton* button) { HandleButtonBoxClicked(button); });

    ui->buttonBox->button(QDialogButtonBox::Save)->setText(tr("Save"));
    ui->buttonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));
    ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Defaults"));
    ui->buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));

    connect(ui->tabWidgetSettings, &QTabWidget::currentChanged, this,
            [this]() { ui->buttonBox->button(QDialogButtonBox::Close)->setFocus(); });

    // GENERAL TAB
    {
        connect(ui->horizontalVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
            VolumeSliderChange(value);

            if (EmulatorState::GetInstance()->IsGameRunning())
                m_ipc_client->adjustVol(value, is_game_specific);
        });

#ifdef ENABLE_UPDATER
        connect(ui->updateCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::gen_checkForUpdates, state == Qt::Checked);
                });

        connect(ui->changelogCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::gen_showChangeLog, state == Qt::Checked);
                });
        connect(ui->checkUpdateButton, &QPushButton::clicked, this, [this]() {
            auto checkUpdate = new CheckUpdate(m_gui_settings, true);
            checkUpdate->exec();
        });
#else
        ui->updaterGroupBox->setVisible(false);
#endif
        connect(ui->updateCompatibilityButton, &QPushButton::clicked, this,
                [this, parent, m_compat_info]() {
                    m_compat_info->UpdateCompatibilityDatabase(this, true);
                    emit CompatibilityChanged();
                });

        connect(ui->enableCompatibilityCheckBox, &QCheckBox::checkStateChanged, this,
                [this, m_compat_info](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::glc_showCompatibility, state);
                    if (state) {
                        m_compat_info->LoadCompatibilityFile();
                    }
                    emit CompatibilityChanged();
                });

        // Audio Device (general)
        connect(
            ui->GenAudioComboBox, &QComboBox::currentTextChanged, this,
            [this](const QString& device) { Config::setMainOutputDevice(device.toStdString()); });

        // Audio Device (DS4 speaker)
        connect(
            ui->DsAudioComboBox, &QComboBox::currentTextChanged, this,
            [this](const QString& device) { Config::setPadSpkOutputDevice(device.toStdString()); });
    }

    // GUI TAB
    {
        connect(ui->backgroundImageOpacitySlider, &QSlider::valueChanged, this,
                [this](int value) { emit BackgroundOpacityChanged(value); });

        connect(ui->BGMVolumeSlider, &QSlider::valueChanged, this,
                [](int value) { BackgroundMusicPlayer::getInstance().setVolume(value); });
        connect(ui->showBackgroundImageCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::gl_showBackgroundImage, state == Qt::Checked);
                });
    }

    // USER TAB
    {
        connect(ui->OpenCustomTrophyLocationButton, &QPushButton::clicked, this, []() {
            QString userPath;
            Common::FS::PathToQString(userPath,
                                      Common::FS::GetUserPath(Common::FS::PathType::CustomTrophy));
            QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
        });

        connect(ui->PortableUserButton, &QPushButton::clicked, this, []() {
            QString launcherDir;
            Common::FS::PathToQString(launcherDir, std::filesystem::current_path() / "launcher");
            if (std::filesystem::exists(std::filesystem::current_path() / "launcher")) {
                QMessageBox::information(NULL, tr("Cannot create portable launcher folder"),
                                         tr("%1 already exists").arg(launcherDir));
            } else {
                std::filesystem::copy(Common::FS::GetUserPath(Common::FS::PathType::LauncherDir),
                                      std::filesystem::current_path() / "launcher",
                                      std::filesystem::copy_options::recursive);
                QMessageBox::information(NULL, tr("Portable launcherDir folder created"),
                                         tr("%1 successfully created.").arg(launcherDir));
            }
        });
    }

    // INPUT TAB
    {
        connect(ui->hideCursorComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](s16 index) { OnCursorStateChanged(index); });
    }

    // PATH TAB
    {
        connect(ui->addFolderButton, &QPushButton::clicked, this, [this]() {
            QString file_path_string =
                QFileDialog::getExistingDirectory(this, tr("Directory to install games"));
            auto file_path = Common::FS::PathFromQString(file_path_string);
            if (!file_path.empty() && Config::addGameInstallDir(file_path, true)) {
                QListWidgetItem* item = new QListWidgetItem(file_path_string);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
                ui->gameFoldersListWidget->addItem(item);
            }
        });

        connect(ui->gameFoldersListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
            ui->removeFolderButton->setEnabled(
                !ui->gameFoldersListWidget->selectedItems().isEmpty());
        });

        connect(ui->removeFolderButton, &QPushButton::clicked, this, [this]() {
            QListWidgetItem* selected_item = ui->gameFoldersListWidget->currentItem();
            QString item_path_string = selected_item ? selected_item->text() : QString();
            if (!item_path_string.isEmpty()) {
                auto file_path = Common::FS::PathFromQString(item_path_string);
                Config::removeGameInstallDir(file_path);
                delete selected_item;
            }
        });

        connect(ui->browseButton, &QPushButton::clicked, this, [this]() {
            const auto save_data_path = Config::GetSaveDataPath();
            QString initial_path;
            Common::FS::PathToQString(initial_path, save_data_path);

            QString save_data_path_string =
                QFileDialog::getExistingDirectory(this, tr("Directory to save data"), initial_path);

            auto file_path = Common::FS::PathFromQString(save_data_path_string);
            if (!file_path.empty()) {
                Config::setSaveDataPath(file_path);
                ui->currentSaveDataPath->setText(save_data_path_string);
            }
        });

        connect(ui->folderButton, &QPushButton::clicked, this, [this]() {
            const auto dlc_folder_path = Config::getAddonInstallDir();
            QString initial_path;
            Common::FS::PathToQString(initial_path, dlc_folder_path);

            QString dlc_folder_path_string =
                QFileDialog::getExistingDirectory(this, tr("Select the DLC folder"), initial_path);

            auto file_path = Common::FS::PathFromQString(dlc_folder_path_string);
            if (!file_path.empty()) {
                Config::setAddonInstallDir(file_path);
                ui->currentDLCFolder->setText(dlc_folder_path_string);
            }
        });

        connect(ui->browse_sysmodules, &QPushButton::clicked, this, [this]() {
            const auto sysmodules_path = Config::getSysModulesPath();
            QString initial_path;
            Common::FS::PathToQString(initial_path, sysmodules_path);

            QString sysmodules_path_string =
                QFileDialog::getExistingDirectory(this, tr("Select the DLC folder"), initial_path);

            auto file_path = Common::FS::PathFromQString(sysmodules_path_string);
            if (!file_path.empty()) {
                Config::setSysModulesPath(file_path);
                ui->sysmodulesPath->setText(sysmodules_path_string);
            }
        });
    }

    // DEBUG TAB
    {
        connect(ui->OpenLogLocationButton, &QPushButton::clicked, this, []() {
            QString userPath;
            Common::FS::PathToQString(userPath,
                                      Common::FS::GetUserPath(Common::FS::PathType::LogDir));
            QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
        });

        // Log presets popup button
        connect(ui->logPresetsButton, &QPushButton::clicked, this, [this]() {
            auto dlg = new LogPresetsDialog(m_gui_settings, this);
            connect(dlg, &LogPresetsDialog::PresetChosen, this,
                    [this](const QString& filter) { ui->logFilterLineEdit->setText(filter); });
            dlg->exec();
        });

        connect(ui->vkValidationCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    state ? ui->vkLayersGroupBox->setVisible(true)
                          : ui->vkLayersGroupBox->setVisible(false);
                });
    }

    // Experimental
    {
        connect(ui->shaderCaheCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    state ? ui->shaderCacheArchiveCheckBox->setVisible(true)
                          : ui->shaderCacheArchiveCheckBox->setVisible(false);
                });
    }

    // GRAPHICS TAB
    connect(ui->RCASSlider, &QSlider::valueChanged, this, [this](int value) {
        QString RCASValue = QString::number(value / 1000.0, 'f', 3);
        ui->RCASValue->setText(RCASValue);
    });

    if (EmulatorState::GetInstance()->IsGameRunning()) {
        connect(ui->RCASSlider, &QSlider::valueChanged, this,
                [this](int value) { m_ipc_client->setRcasAttenuation(value); });
        connect(ui->FSRCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) { m_ipc_client->setFsr(state); });

        connect(ui->RCASCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) { m_ipc_client->setRcas(state); });
    }

    // Descriptions
    {
        // General
        ui->consoleLanguageGroupBox->installEventFilter(this);
        ui->emulatorLanguageGroupBox->installEventFilter(this);
        ui->showSplashCheckBox->installEventFilter(this);
        ui->discordRPCCheckbox->installEventFilter(this);
        ui->volumeSliderElement->installEventFilter(this);
#ifdef ENABLE_UPDATER
        ui->updaterGroupBox->installEventFilter(this);
#endif

        // GUI
        ui->GUIBackgroundImageGroupBox->installEventFilter(this);
        ui->GUIMusicGroupBox->installEventFilter(this);
        ui->enableCompatibilityCheckBox->installEventFilter(this);
        ui->checkCompatibilityOnStartupCheckBox->installEventFilter(this);
        ui->updateCompatibilityButton->installEventFilter(this);

        // User
        ui->userName->installEventFilter(this);
        ui->disableTrophycheckBox->installEventFilter(this);
        ui->OpenCustomTrophyLocationButton->installEventFilter(this);
        ui->label_Trophy->installEventFilter(this);
        ui->trophyKeyLineEdit->installEventFilter(this);
        ui->PortableUserFolderGroupBox->installEventFilter(this);

        // Input
        ui->hideCursorGroupBox->installEventFilter(this);
        ui->idleTimeoutGroupBox->installEventFilter(this);
        ui->backgroundControllerCheckBox->installEventFilter(this);
        ui->motionControlsCheckBox->installEventFilter(this);
        ui->micComboBox->installEventFilter(this);
        ui->usbComboBox->installEventFilter(this);

        // Graphics
        ui->graphicsAdapterGroupBox->installEventFilter(this);
        ui->windowSizeGroupBox->installEventFilter(this);
        ui->presentModeGroupBox->installEventFilter(this);
        ui->heightDivider->installEventFilter(this);
        ui->nullGpuCheckBox->installEventFilter(this);
        ui->enableHDRCheckBox->installEventFilter(this);
        ui->chooseHomeTabGroupBox->installEventFilter(this);

        // Paths
        ui->gameFoldersGroupBox->installEventFilter(this);
        ui->gameFoldersListWidget->installEventFilter(this);
        ui->addFolderButton->installEventFilter(this);
        ui->removeFolderButton->installEventFilter(this);
        ui->saveDataGroupBox->installEventFilter(this);
        ui->sysmodulesGroupBox->installEventFilter(this);
        ui->browse_sysmodules->installEventFilter(this);
        ui->currentSaveDataPath->installEventFilter(this);
        ui->currentDLCFolder->installEventFilter(this);
        ui->browseButton->installEventFilter(this);
        ui->folderButton->installEventFilter(this);

        // Log
        ui->logTypeGroupBox->installEventFilter(this);
        ui->logFilter->installEventFilter(this);
        ui->enableLoggingCheckBox->installEventFilter(this);
        ui->separateLogFilesCheckbox->installEventFilter(this);
        ui->OpenLogLocationButton->installEventFilter(this);

        // Debug
        ui->debugDump->installEventFilter(this);
        ui->vkValidationCheckBox->installEventFilter(this);
        ui->vkSyncValidationCheckBox->installEventFilter(this);
        ui->vkCoreValidationCheckBox->installEventFilter(this);
        ui->vkGpuValidationCheckBox->installEventFilter(this);
        ui->rdocCheckBox->installEventFilter(this);
        ui->crashDiagnosticsCheckBox->installEventFilter(this);
        ui->guestMarkersCheckBox->installEventFilter(this);
        ui->hostMarkersCheckBox->installEventFilter(this);
        ui->collectShaderCheckBox->installEventFilter(this);
        ui->copyGPUBuffersCheckBox->installEventFilter(this);

        // Experimental
        ui->readbacksCheckBox->installEventFilter(this);
        ui->readbackLinearImagesCheckBox->installEventFilter(this);
        ui->dumpShadersCheckBox->installEventFilter(this);
        ui->dmaCheckBox->installEventFilter(this);
        ui->devkitCheckBox->installEventFilter(this);
        ui->neoCheckBox->installEventFilter(this);
        ui->networkConnectedCheckBox->installEventFilter(this);
        ui->shaderCaheCheckBox->installEventFilter(this);
        ui->shaderCacheArchiveCheckBox->installEventFilter(this);
        ui->psnSignInCheckBox->installEventFilter(this);
        ui->dmemGroupBox->installEventFilter(this);
    }

    SDL_InitSubSystem(SDL_INIT_EVENTS);
    Polling = QtConcurrent::run(&SettingsDialog::pollSDLevents, this);
}

void SettingsDialog::closeEvent(QCloseEvent* event) {
    if (!is_game_saving) {
        if (ShouldApplyToTab(SettingsTabId::Gui, scoped_tab_id) && !is_game_specific) {
            ui->backgroundImageOpacitySlider->setValue(backgroundImageOpacitySlider_backup);
            emit BackgroundOpacityChanged(backgroundImageOpacitySlider_backup);
            ui->BGMVolumeSlider->setValue(bgm_volume_backup);
            BackgroundMusicPlayer::getInstance().setVolume(bgm_volume_backup);
        }

        toml::value data;
        toml::value gs_data;
        if (LoadSyncConfigData(data, gs_data)) {
            SyncRealTimeWidgetsForScope(data, gs_data);
        }
    }

    SDL_Event quitLoop{};
    quitLoop.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quitLoop);
    Polling.waitForFinished();

    SDL_QuitSubSystem(SDL_INIT_EVENTS);

    // This breaks the microphone selection
    // SDL_QuitSubSystem(SDL_INIT_AUDIO);
    // SDL_Quit();

    QDialog::closeEvent(event);
}

void SettingsDialog::LoadValuesFromConfig() {
    std::error_code error;
    bool is_newly_created = false;
    if (!std::filesystem::exists(config_file, error)) {
        Config::save(config_file, is_game_specific);
        is_newly_created = true;
    }

    toml::value data;
    try {
        data = toml::parse(config_file);
    } catch (std::exception& ex) {
        fmt::print("Got exception trying to load config file. Exception: {}\n", ex.what());
        return;
    }

    LoadValuesForScope(data, data, is_newly_created);

    if (!is_game_specific) {
        toml::value sync_data;
        toml::value gs_data;
        if (LoadSyncConfigData(sync_data, gs_data)) {
            SyncRealTimeWidgetsForScope(sync_data, gs_data);
        }
    }
}

void SettingsDialog::InitializeEmulatorLanguages() {
    QDirIterator it(QStringLiteral(":/translations"), QDirIterator::NoIteratorFlags);

    QVector<QPair<QString, QString>> languagesList;

    while (it.hasNext()) {
        QString locale = it.next();
        locale.truncate(locale.lastIndexOf(QLatin1Char{'.'}));
        locale.remove(0, locale.lastIndexOf(QLatin1Char{'/'}) + 1);
        const QString lang = QLocale::languageToString(QLocale(locale).language());
        const QString country = QLocale::territoryToString(QLocale(locale).territory());

        QString displayName = QStringLiteral("%1 (%2)").arg(lang, country);
        languagesList.append(qMakePair(locale, displayName));
    }

    std::sort(languagesList.begin(), languagesList.end(),
              [](const QPair<QString, QString>& a, const QPair<QString, QString>& b) {
                  return a.second < b.second;
              });

    int idx = 0;
    for (const auto& pair : languagesList) {
        const QString& locale = pair.first;
        const QString& displayName = pair.second;

        ui->emulatorLanguageComboBox->addItem(displayName, locale);
        languages[locale.toStdString()] = idx;
        idx++;
    }

    connect(ui->emulatorLanguageComboBox, &QComboBox::currentIndexChanged, this,
            &SettingsDialog::OnLanguageChanged);
}

void SettingsDialog::OnLanguageChanged(int index) {
    if (index == -1)
        return;

    ui->retranslateUi(this);

    emit LanguageChanged(ui->emulatorLanguageComboBox->itemData(index).toString());
}

void SettingsDialog::OnCursorStateChanged(s16 index) {
    if (index == -1)
        return;
    if (index == Config::HideCursorState::Idle) {
        ui->idleTimeoutGroupBox->show();
    } else {
        if (!ui->idleTimeoutGroupBox->isHidden()) {
            ui->idleTimeoutGroupBox->hide();
        }
    }
}

void SettingsDialog::VolumeSliderChange(int value) {
    ui->volumeText->setText(QString::number(ui->horizontalVolumeSlider->sliderPosition()) + "%");
}

int SettingsDialog::exec() {
    return QDialog::exec();
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::updateNoteTextEdit(const QString& elementName) {
    QString text;

    // clang-format off
    // General
    if (elementName == "consoleLanguageGroupBox") {
        text = tr("Console Language:\\n"
                  "Sets the language that the PS4 game uses.\\n"
                  "It's recommended to set this to a language the game supports, "
                  "which will vary by region.");
    } else if (elementName == "emulatorLanguageGroupBox") {
        text = tr("Emulator Language:\\nSets the language of the emulator's user interface.");
    } else if (elementName == "showSplashCheckBox") {
        text = tr("Show Splash Screen:\\n"
                  "Shows the game's splash screen (a special image) while the game is starting.");
    } else if (elementName == "discordRPCCheckbox") {
        text = tr("Enable Discord Rich Presence:\\n"
                  "Displays the emulator icon and relevant information on your Discord profile.");
    } else if (elementName == "userName") {
        text = tr("Username:\\n"
                  "Sets the PS4's account username, which may be displayed by some games.");
    } else if (elementName == "label_Trophy" || elementName == "trophyKeyLineEdit") {
        text = tr("Trophy Key:\\n"
                  "Key used to decrypt trophies. Must be obtained from your jailbroken "
                  "console.\\n"
                  "Must contain only hex characters.");
    } else if (elementName == "logTypeGroupBox") {
        text = tr("Log Type:\\n"
                  "Sets whether to synchronize the output of the log window for performance. "
                  "May have adverse effects on emulation.");
    } else if (elementName == "logFilter") {
        text = tr("Log Filter:\\n"
                  "Filters the log to only print specific information.\\n"
                  "Examples: \"Core:Trace\" \"Lib.Pad:Debug Common.Filesystem:Error\" "
                  "\"*:Critical\"\\n"
                  "Levels: Trace, Debug, Info, Warning, Error, Critical - in this order, a "
                  "specific level silences all levels preceding it in the list and logs every "
                  "level after it.");
    #ifdef ENABLE_UPDATER
    } else if (elementName == "updaterGroupBox") {
        text = tr("GUI Updates:\\n"
                  "Release: Official versions released every month that may be very outdated, "
                  "but are more reliable and tested.\\n"
                  "Nightly: Development versions that have all the latest features and fixes, "
                  "but may contain bugs and are less stable.\\n\\n"
                  "*This update applies only to the Qt user interface. To update the emulator "
                  "core, please use the 'Version Manager' menu.");
#endif
    } else if (elementName == "GUIBackgroundImageGroupBox") {
        text = tr("Background Image:\\nControl the opacity of the game background image.");
    } else if (elementName == "GUIMusicGroupBox") {
        text = tr("Play Title Music:\\n"
                  "If a game supports it, enable playing special music when selecting the "
                  "game in the GUI.");
    } else if (elementName == "enableHDRCheckBox") {
        text = tr("Enable HDR:\\n"
                  "Enables HDR in games that support it.\\n"
                  "Your monitor must have support for the BT2020 PQ color space and the RGB10A2 "
                  "swapchain format.");
    } else if (elementName == "disableTrophycheckBox") {
        text = tr("Disable Trophy Pop-ups:\\n"
                  "Disable in-game trophy notifications. Trophy progress can still be tracked "
                  "using the Trophy Viewer (right-click the game in the main window).");
    } else if (elementName == "enableCompatibilityCheckBox") {
        text = tr("Display Compatibility Data:\\n"
                  "Displays game compatibility information in table view. Enable "
                  "\"Update Compatibility On Startup\" to get up-to-date information.");
    } else if (elementName == "checkCompatibilityOnStartupCheckBox") {
        text = tr("Update Compatibility On Startup:\\n"
                  "Automatically update the compatibility database when shadPS4 starts.");
    } else if (elementName == "updateCompatibilityButton") {
        text = tr("Update Compatibility Database:\\n"
                  "Immediately update the compatibility database.");
    }

    //User
    if (elementName == "OpenCustomTrophyLocationButton") {
        text = tr("Open the custom trophy images/sounds folder:\\n"
                  "You can add custom images to the trophies and an audio.\\n"
                  "Add the files to custom_trophy with the following names:\\n"
                  "trophy.wav OR trophy.mp3, bronze.png, gold.png, platinum.png, silver.png\\n"
                  "Note: The sound will only work in QT versions.");
    }

    // Input
    if (elementName == "hideCursorGroupBox") {
        text = tr("Hide Cursor:\\n"
                  "Choose when the cursor will disappear:\\n"
                  "Never: You will always see the mouse.\\n"
                  "idle: Set a time for it to disappear after being idle.\\n"
                  "Always: you will never see the mouse.");
    } else if (elementName == "idleTimeoutGroupBox") {
        text = tr("Hide Idle Cursor Timeout:\\n"
                  "The duration (seconds) after which the cursor that has been idle hides "
                  "itself.");
    } else if (elementName == "backgroundControllerCheckBox") {
        text = tr("Enable Controller Background Input:\\n"
                  "Allow shadPS4 to detect controller inputs when the game window is not in "
                  "focus.");
    }

    // Graphics
    if (elementName == "graphicsAdapterGroupBox") {
        text = tr("Graphics Device:\\n"
                  "On multiple GPU systems, select the GPU the emulator will use from the drop "
                  "down list,\\n"
                  "or select \"Auto Select\" to automatically determine it.");
    } else if (elementName == "presentModeGroupBox") {
        text = tr("Present Mode:\\n"
                  "Configures how video output will be presented to your screen.\\n\\n"
                  "Mailbox: Frames synchronize with your screen's refresh rate. New frames will "
                  "replace any pending frames. Reduces latency but may skip frames if running "
                  "behind.\\n"
                  "Fifo: Frames synchronize with your screen's refresh rate. New frames will be "
                  "queued behind pending frames. Ensures all frames are presented but may "
                  "increase latency.\\n"
                  "Immediate: Frames immediately present to your screen when ready. May result "
                  "in tearing.");
    } else if (elementName == "windowSizeGroupBox") {
        text = tr("Width/Height:\\n"
                  "Sets the size of the emulator window at launch, which can be resized during "
                  "gameplay.\\n"
                  "This is different from the in-game resolution.");
    } else if (elementName == "heightDivider") {
        text = tr("Vblank Frequency:\\n"
                  "The frame rate at which the emulator refreshes at (60hz is the baseline, "
                  "whether the game runs at 30 or 60fps). Changing this may have adverse "
                  "effects, such as increasing the game speed, or breaking critical game "
                  "functionality that does not expect this to change!");
    } else if (elementName == "dumpShadersCheckBox") {
        text = tr("Enable Shaders Dumping:\\n"
                  "For the sake of technical debugging, saves the game's shaders to a folder "
                  "as they render.");
    } else if (elementName == "nullGpuCheckBox") {
        text = tr("Enable Null GPU:\\n"
                  "For the sake of technical debugging, disables game rendering as if there "
                  "were no graphics card. The screen will be black.");
    }

    // Path
    if (elementName == "gameFoldersGroupBox" || elementName == "gameFoldersListWidget") {
        text = tr("Game Folders:\\nThe list of folders to check for installed games.");
    } else if (elementName == "addFolderButton") {
        text = tr("Add:\\nAdd a folder to the list.");
    } else if (elementName == "removeFolderButton") {
        text = tr("Remove:\\nRemove a folder from the list.");
    } else if (elementName == "PortableUserFolderGroupBox") {
        text = tr("Portable user folder:\\n"
                  "Stores shadPS4 settings and data that will be applied only to the shadPS4 "
                  "build located in the current folder. Restart the app after creating the "
                  "portable user folder to begin using it.");
    } else if (elementName == "sysmodulesGroupBox") {
        text = tr("PS4 Sysmodules Path:\\nThe folder where PS4 sysmodules are loaded from.");
    } else if (elementName == "browse_sysmodules") {
        text = tr("Browse:\\nBrowse for a folder to set as the sysmodules path.");
    }

    // DLC Folder
    if (elementName == "dlcFolderGroupBox" || elementName == "currentDLCFolder") {
        text = tr("DLC Path:\\nThe folder where game DLC is loaded from.");
    } else if (elementName == "folderButton") {
        text = tr("Browse:\\nBrowse for a folder to set as the DLC path.");
    }

    // Save Data
    if (elementName == "saveDataGroupBox" || elementName == "currentSaveDataPath") {
        text = tr("Save Data Path:\\nThe folder where game save data will be saved.");
    } else if (elementName == "browseButton") {
        text = tr("Browse:\\nBrowse for a folder to set as the save data path.");
    }

    // Debug
    if (elementName == "debugDump") {
        text = tr("Enable Debug Dumping:\\n"
                  "Saves the import and export symbols and file header information of the "
                  "currently running PS4 program to a directory.");
    } else if (elementName == "vkValidationCheckBox") {
        text = tr("Enable Vulkan Validation Layers:\\n"
                  "Enables a system that validates the state of the Vulkan renderer and logs "
                  "information about its internal state.\\n"
                  "This will reduce performance and likely change the behavior of emulation.\\n"
                  "You need the Vulkan SDK for this to work.");
    } else if (elementName == "vkSyncValidationCheckBox") {
        text = tr("Enable Sync Validation:\\n"
                  "Enables a system that validates the timing of Vulkan rendering tasks.\\n"
                  "This will reduce performance and likely change the behavior of emulation.\\n"
                  "You need the Vulkan SDK for this to work.");
    } else if (elementName == "vkCoreValidationCheckBox") {
        text = tr("Enable Core Validation:\\n"
                  "Enables the main API validation functions.\\n"
                  "This will reduce performance and likely change the behavior of emulation.\\n"
                  "You need the Vulkan SDK for this to work.");
    } else if (elementName == "vkGpuValidationCheckBox") {
        text = tr("Enable GPU-Assisted Validation:\\n"
                  "Instruments shaders with code that validates if they are behaving correctly."
                  "\\n"
                  "This will reduce performance and likely change the behavior of emulation.\\n"
                  "You need the Vulkan SDK for this to work.");
    } else if (elementName == "rdocCheckBox") {
        text = tr("Enable RenderDoc Debugging:\\n"
                  "If enabled, the emulator will provide compatibility with Renderdoc to allow "
                  "capture and analysis of the currently rendered frame.");
    } else if (elementName == "crashDiagnosticsCheckBox") {
        text = tr("Crash Diagnostics:\\n"
                  "Creates a .yaml file with info about the Vulkan state at the time of "
                  "crashing.\\n"
                  "Useful for debugging 'Device lost' errors. If you have this enabled, you "
                  "should enable Host AND Guest Debug Markers.\\n"
                  "You need Vulkan Validation Layers enabled and the Vulkan SDK for this to "
                  "work.");
    } else if (elementName == "guestMarkersCheckBox") {
        text = tr("Guest Debug Markers:\\n"
                  "Inserts any debug markers the game itself has added to the command buffer."
                  "\\n"
                  "If you have this enabled, you should enable Crash Diagnostics.\\n"
                  "Useful for programs like RenderDoc.");
    } else if (elementName == "hostMarkersCheckBox") {
        text = tr("Host Debug Markers:\\n"
                  "Inserts emulator-side information like markers for specific AMDGPU commands "
                  "around Vulkan commands, as well as giving resources debug names.\\n"
                  "If you have this enabled, you should enable Crash Diagnostics.\\n"
                  "Useful for programs like RenderDoc.");
    } else if (elementName == "copyGPUBuffersCheckBox") {
        text = tr("Copy GPU Buffers:\\n"
                  "Gets around race conditions involving GPU submits.\\n"
                  "May or may not help with PM4 type 0 crashes.");
    } else if (elementName == "collectShaderCheckBox") {
        text = tr("Collect Shaders:\\n"
                  "You need this enabled to edit shaders with the debug menu (Ctrl + F10).");
    } else if (elementName == "separateLogFilesCheckbox") {
        text = tr("Separate Log Files:\\nWrites a separate logfile for each game.");
    } else if (elementName == "enableLoggingCheckBox") {
        text = tr("Enable Logging:\\n"
                  "Enables logging.\\n"
                  "Do not change this if you do not know what you're doing!\\n"
                  "When asking for help, make sure this setting is ENABLED.");
    } else if (elementName == "OpenLogLocationButton") {
        text = tr("Open Log Location:\\nOpen the folder where the log file is saved.");
    } else if (elementName == "micComboBox") {
        text = tr("Microphone:\\n"
                  "None: Does not use the microphone.\\n"
                  "Default Device: Will use the default device defined in the system.\\n"
                  "Or manually choose the microphone to be used from the list.");
    } else if (elementName == "volumeSliderElement") {
        text = tr("Volume:\\n"
                  "Adjust volume for games on a global level, range goes from 0-500% with the "
                  "default being 100%.");
    } else if (elementName == "chooseHomeTabGroupBox") {
        text = tr("Default tab when opening settings:\\n"
                  "Choose which tab will open, the default is General.");
    } else if (elementName == "motionControlsCheckBox") {
        text = tr("Enable Motion Controls:\\n"
                  "When enabled it will use the controller's motion control if supported.");
    } else if (elementName == "usbComboBox") {
        text = tr("USB Device:\\n"
                  "Real USB Device: Use a real USB Device attached to the system.\\n"
                  "Skylander Portal: Emulate a Skylander Portal of Power.\\n"
                  "Infinity Base: Emulate a Disney Infinity Base.\\n"
                  "Dimensions Toypad: Emulate a Lego Dimensions Toypad.");
    }

    // Experimental
    if (elementName == "dmaCheckBox") {
        text = tr("Enable Direct Memory Access:\\n"
                  "Enables arbitrary memory access from the GPU to CPU memory.");
    } else if (elementName == "neoCheckBox") {
        text = tr("Enable PS4 Neo Mode:\\n"
                  "Adds support for PS4 Pro emulation and memory size. Currently causes "
                  "instability in a large number of tested games.");
    } else if (elementName == "devkitCheckBox") {
        text = tr("Enable Devkit Console Mode:\\nAdds support for Devkit console memory size.");
    } else if (elementName == "networkConnectedCheckBox") {
        text = tr("Set Network Connected to True:\\n"
                  "Forces games to detect an active network connection. Actual online "
                  "capabilities are not yet supported.");
    } else if (elementName == "shaderCaheCheckBox") {
        text = tr("Enable Shader Cache:\\n"
                  "Storing compiled shaders to avoid recompilations, reduce stuttering.");
    } else if (elementName == "shaderCacheArchiveCheckBox") {
        text = tr("Compress the Shader Cache files into a zip file:\\n"
                  "The shader cache files are stored within a single zip file instead of "
                  "multiple separate files.");
    } else if (elementName == "psnSignInCheckBox") {
        text = tr("Set PSN Signed-in to True:\\n"
                  "Forces games to detect an active PSN sign-in. Actual PSN capabilities are "
                  "not supported.");
    } else if (elementName == "readbacksCheckBox") {
        text = tr("Enable Readbacks:\\n"
                  "Enable GPU memory readbacks and writebacks.\\n"
                  "This is required for proper behavior in some games.\\n"
                  "Might cause stability and/or performance issues.");
    } else if (elementName == "readbackLinearImagesCheckBox") {
        text = tr("Enable Readback Linear Images:\\n"
                  "Enables async downloading of GPU modified linear images.\\n"
                  "Might fix issues in some games.");
    } else if (elementName == "dmemGroupBox") {
        text = tr("Additional DMem Allocation:\\n"
                  "Forces allocation of the specified amount of additional DMem. Crashes or "
                  "causes issues in some games.");
    }

    // clang-format on
    ui->descriptionText->setText(text.replace("\\n", "\n"));
}

bool SettingsDialog::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        if (qobject_cast<QWidget*>(obj)) {
            bool hovered = (event->type() == QEvent::Enter);
            QString elementName = obj->objectName();
            if (hovered) {
                updateNoteTextEdit(elementName);
            } else {
                ui->descriptionText->setText(defaultTextEdit);
            }
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

QTabWidget* SettingsDialog::GetTabWidget() const {
    return ui->tabWidgetSettings;
}

QDialogButtonBox* SettingsDialog::GetButtonBox() const {
    return ui->buttonBox;
}

QTextEdit* SettingsDialog::GetDescriptionText() const {
    return ui->descriptionText;
}

void SettingsDialog::SetCloseTarget(QWidget* target) {
    close_target = target ? target : this;
}

void SettingsDialog::SetSingleTabScope(const QString& tab_object_name) {
    scoped_tab_id = TabIdFromObjectName(tab_object_name);
}

void SettingsDialog::ClearTabScope() {
    scoped_tab_id = SettingsTabId::Unknown;
}

SettingsDialog::SettingsTabId SettingsDialog::TabIdFromObjectName(const QString& object_name) {
    if (object_name == "generalTab") {
        return SettingsTabId::General;
    }
    if (object_name == "guiTab") {
        return SettingsTabId::Gui;
    }
    if (object_name == "graphicsTab") {
        return SettingsTabId::Graphics;
    }
    if (object_name == "userTab") {
        return SettingsTabId::User;
    }
    if (object_name == "inputTab") {
        return SettingsTabId::Input;
    }
    if (object_name == "pathsTab") {
        return SettingsTabId::Paths;
    }
    if (object_name == "logTab") {
        return SettingsTabId::Log;
    }
    if (object_name == "debugTab") {
        return SettingsTabId::Debug;
    }
    if (object_name == "experimentalTab") {
        return SettingsTabId::Experimental;
    }
    return SettingsTabId::Unknown;
}

bool SettingsDialog::ShouldApplyToTab(SettingsTabId tab_id, SettingsTabId scope_tab_id) {
    if (scope_tab_id == SettingsTabId::Unknown) {
        return true;
    }
    return tab_id == scope_tab_id;
}

void SettingsDialog::HandleButtonBoxClicked(QAbstractButton* button) {
    if (button == ui->buttonBox->button(QDialogButtonBox::Save)) {
        is_game_saving = true;
        ApplySettingsForScope();
        Config::save(config_file, is_game_specific);
        close_target->close();
    } else if (button == ui->buttonBox->button(QDialogButtonBox::Apply)) {
        ApplySettingsForScope();
        Config::save(config_file, is_game_specific);
    } else if (button == ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)) {
        SetDefaultValuesForScope();
        if (scoped_tab_id == SettingsTabId::Unknown) {
            Config::setDefaultValues(is_game_specific);
        }
        Config::save(config_file, is_game_specific);
        LoadValuesFromConfig();
    } else if (button == ui->buttonBox->button(QDialogButtonBox::Close)) {
        if (ShouldApplyToTab(SettingsTabId::Gui, scoped_tab_id) && !is_game_specific) {
            ui->backgroundImageOpacitySlider->setValue(backgroundImageOpacitySlider_backup);
            emit BackgroundOpacityChanged(backgroundImageOpacitySlider_backup);
            ui->BGMVolumeSlider->setValue(bgm_volume_backup);
            BackgroundMusicPlayer::getInstance().setVolume(bgm_volume_backup);
        }

        toml::value data;
        toml::value gs_data;
        if (LoadSyncConfigData(data, gs_data)) {
            SyncRealTimeWidgetsForScope(data, gs_data);
        }
    }

    if (Common::Log::IsActive()) {
        Common::Log::Filter filter;
        filter.ParseFilterString(Config::getLogFilter());
        Common::Log::SetGlobalFilter(filter);
    }
}

void SettingsDialog::ApplySettingsForScope() {
    if (scoped_tab_id == SettingsTabId::Unknown) {
        ApplySettingsForTab(SettingsTabId::General);
        ApplySettingsForTab(SettingsTabId::Gui);
        ApplySettingsForTab(SettingsTabId::Graphics);
        ApplySettingsForTab(SettingsTabId::User);
        ApplySettingsForTab(SettingsTabId::Input);
        ApplySettingsForTab(SettingsTabId::Paths);
        ApplySettingsForTab(SettingsTabId::Log);
        ApplySettingsForTab(SettingsTabId::Debug);
        ApplySettingsForTab(SettingsTabId::Experimental);
        return;
    }

    ApplySettingsForTab(scoped_tab_id);
}

void SettingsDialog::ApplySettingsForTab(SettingsTabId tab_id) {
    const bool is_specific = is_game_specific;

    switch (tab_id) {
    case SettingsTabId::General: {
        Config::setVolumeSlider(ui->horizontalVolumeSlider->value(), is_specific);
        Config::setLanguage(languageIndexes[ui->consoleLanguageComboBox->currentIndex()],
                            is_specific);
        Config::setShowSplash(ui->showSplashCheckBox->isChecked(), is_specific);
        Config::setMainOutputDevice(ui->GenAudioComboBox->currentText().toStdString(), is_specific);
        Config::setPadSpkOutputDevice(ui->DsAudioComboBox->currentText().toStdString(),
                                      is_specific);
        break;
    }
    case SettingsTabId::Gui: {
        if (!is_specific) {
            BackgroundMusicPlayer::getInstance().setVolume(ui->BGMVolumeSlider->value());
            Config::setTrophyKey(ui->trophyKeyLineEdit->text().toStdString());
            Config::setEnableDiscordRPC(ui->discordRPCCheckbox->isChecked());
            m_gui_settings->SetValue(gui::glc_showCompatibility,
                                     ui->enableCompatibilityCheckBox->isChecked());
            m_gui_settings->SetValue(gui::gen_checkCompatibilityAtStartup,
                                     ui->checkCompatibilityOnStartupCheckBox->isChecked());
            m_gui_settings->SetValue(gui::gl_playBackgroundMusic, ui->playBGMCheckBox->isChecked());
            m_gui_settings->SetValue(gui::gl_backgroundMusicVolume, ui->BGMVolumeSlider->value());
            m_gui_settings->SetValue(gui::gen_checkForUpdates, ui->updateCheckBox->isChecked());
            m_gui_settings->SetValue(gui::gen_showChangeLog, ui->changelogCheckBox->isChecked());
            m_gui_settings->SetValue(gui::gl_showBackgroundImage,
                                     ui->showBackgroundImageCheckBox->isChecked());
            m_gui_settings->SetValue(gui::gl_backgroundImageOpacity,
                                     std::clamp(ui->backgroundImageOpacitySlider->value(), 0, 100));
            emit BackgroundOpacityChanged(ui->backgroundImageOpacitySlider->value());
            m_gui_settings->SetValue(
                gui::gen_homeTab, chooseHomeTabMap.value(ui->chooseHomeTabComboBox->currentText()));
        }
        break;
    }
    case SettingsTabId::Graphics: {
        Config::setIsFullscreen(
            screenModeMap.value(ui->displayModeComboBox->currentText()) != "Windowed", is_specific);
        Config::setFullscreenMode(
            screenModeMap.value(ui->displayModeComboBox->currentText()).toStdString(), is_specific);
        Config::setPresentMode(
            presentModeMap.value(ui->presentModeComboBox->currentText()).toStdString(),
            is_specific);
        Config::setAllowHDR(ui->enableHDRCheckBox->isChecked(), is_specific);
        Config::setGpuId(ui->graphicsAdapterBox->currentIndex() - 1, is_specific);
        Config::setWindowWidth(ui->widthSpinBox->value(), is_specific);
        Config::setWindowHeight(ui->heightSpinBox->value(), is_specific);
        Config::setDumpShaders(ui->dumpShadersCheckBox->isChecked(), is_specific);
        Config::setNullGpu(ui->nullGpuCheckBox->isChecked(), is_specific);
        Config::setFsrEnabled(ui->FSRCheckBox->isChecked(), is_specific);
        Config::setRcasEnabled(ui->RCASCheckBox->isChecked(), is_specific);
        Config::setRcasAttenuation(ui->RCASSlider->value(), is_specific);
        break;
    }
    case SettingsTabId::User: {
        Config::setUserName(ui->userNameLineEdit->text().toStdString(), is_specific);
        Config::setisTrophyPopupDisabled(ui->disableTrophycheckBox->isChecked(), is_specific);
        Config::setTrophyNotificationDuration(ui->popUpDurationSpinBox->value(), is_specific);

        if (ui->radioButton_Top->isChecked()) {
            Config::setSideTrophy("top", is_specific);
        } else if (ui->radioButton_Left->isChecked()) {
            Config::setSideTrophy("left", is_specific);
        } else if (ui->radioButton_Right->isChecked()) {
            Config::setSideTrophy("right", is_specific);
        } else if (ui->radioButton_Bottom->isChecked()) {
            Config::setSideTrophy("bottom", is_specific);
        }
        break;
    }
    case SettingsTabId::Input: {
        Config::setIsMotionControlsEnabled(ui->motionControlsCheckBox->isChecked(), is_specific);
        Config::setBackgroundControllerInput(ui->backgroundControllerCheckBox->isChecked(),
                                             is_specific);
        Config::setCursorState(ui->hideCursorComboBox->currentIndex(), is_specific);
        Config::setCursorHideTimeout(ui->hideCursorComboBox->currentIndex(), is_specific);
        Config::setMicDevice(ui->micComboBox->currentData().toString().toStdString(), is_specific);
        Config::setUsbDeviceBackend(ui->usbComboBox->currentIndex(), is_specific);
        break;
    }
    case SettingsTabId::Paths: {
        if (!is_specific) {
            std::vector<Config::GameInstallDir> dirs_with_states;
            for (int i = 0; i < ui->gameFoldersListWidget->count(); i++) {
                QListWidgetItem* item = ui->gameFoldersListWidget->item(i);
                QString path_string = item->text();
                auto path = Common::FS::PathFromQString(path_string);
                bool enabled = (item->checkState() == Qt::Checked);

                dirs_with_states.push_back({path, enabled});
            }
            Config::setAllGameInstallDirs(dirs_with_states);
        }
        break;
    }
    case SettingsTabId::Log: {
        Config::setLoggingEnabled(ui->enableLoggingCheckBox->isChecked(), is_specific);
        Config::setLogType(logTypeMap.value(ui->logTypeComboBox->currentText()).toStdString(),
                           is_specific);
        Config::setLogFilter(ui->logFilterLineEdit->text().toStdString(), is_specific);
        Config::setSeparateLogFilesEnabled(ui->separateLogFilesCheckbox->isChecked(), is_specific);
        break;
    }
    case SettingsTabId::Debug: {
        Config::setDebugDump(ui->debugDump->isChecked(), is_specific);
        Config::setVkValidation(ui->vkValidationCheckBox->isChecked(), is_specific);
        Config::setVkSyncValidation(ui->vkSyncValidationCheckBox->isChecked(), is_specific);
        Config::setVkCoreValidation(ui->vkCoreValidationCheckBox->isChecked(), is_specific);
        Config::setVkGpuValidation(ui->vkGpuValidationCheckBox->isChecked(), is_specific);
        Config::setRdocEnabled(ui->rdocCheckBox->isChecked(), is_specific);
        Config::setVkHostMarkersEnabled(ui->hostMarkersCheckBox->isChecked(), is_specific);
        Config::setVkGuestMarkersEnabled(ui->guestMarkersCheckBox->isChecked(), is_specific);
        Config::setVkCrashDiagnosticEnabled(ui->crashDiagnosticsCheckBox->isChecked(), is_specific);
        Config::setCollectShaderForDebug(ui->collectShaderCheckBox->isChecked(), is_specific);
        Config::setCopyGPUCmdBuffers(ui->copyGPUBuffersCheckBox->isChecked(), is_specific);
        break;
    }
    case SettingsTabId::Experimental: {
        Config::setReadbacks(ui->readbacksCheckBox->isChecked(), is_specific);
        Config::setReadbackLinearImages(ui->readbackLinearImagesCheckBox->isChecked(), is_specific);
        Config::setDirectMemoryAccess(ui->dmaCheckBox->isChecked(), is_specific);
        Config::setDevKitConsole(ui->devkitCheckBox->isChecked(), is_specific);
        Config::setNeoMode(ui->neoCheckBox->isChecked(), is_specific);
        Config::setConnectedToNetwork(ui->networkConnectedCheckBox->isChecked(), is_specific);
        Config::setPipelineCacheEnabled(ui->shaderCaheCheckBox->isChecked(), is_specific);
        Config::setPipelineCacheArchived(ui->shaderCacheArchiveCheckBox->isChecked(), is_specific);
        Config::setPSNSignedIn(ui->psnSignInCheckBox->isChecked(), is_specific);
        Config::setVblankFreq(ui->vblankSpinBox->value(), is_specific);
        Config::setExtraDmemInMbytes(ui->dmemSpinBox->value(), is_specific);
        break;
    }
    case SettingsTabId::Unknown:
        break;
    }
}

void SettingsDialog::LoadValuesForScope(const toml::value& data, const toml::value& gs_data,
                                        bool is_newly_created) {
    if (scoped_tab_id == SettingsTabId::Unknown) {
        LoadValuesForTab(SettingsTabId::General, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::Gui, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::Graphics, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::User, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::Input, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::Paths, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::Log, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::Debug, data, gs_data, is_newly_created);
        LoadValuesForTab(SettingsTabId::Experimental, data, gs_data, is_newly_created);

        if (!is_game_specific) {
            QString chooseHomeTab = m_gui_settings->GetValue(gui::gen_homeTab).toString();
            QString translatedText = chooseHomeTabMap.key(chooseHomeTab);
            if (translatedText.isEmpty()) {
                translatedText = tr("General");
            }
            ui->chooseHomeTabComboBox->setCurrentText(translatedText);

            QStringList tabNames = {tr("General"), tr("Frontend"), tr("Graphics"),
                                    tr("User"),    tr("Input"),    tr("Paths"),
                                    tr("Log"),     tr("Debug"),    tr("Experimental")};
            int indexTab = tabNames.indexOf(translatedText);
            if (indexTab == -1 || !ui->tabWidgetSettings->isTabVisible(indexTab) ||
                is_newly_created) {
                indexTab = 0;
            }
            ui->tabWidgetSettings->setCurrentIndex(indexTab);
        }
        return;
    }

    LoadValuesForTab(scoped_tab_id, data, gs_data, is_newly_created);
}

void SettingsDialog::LoadValuesForTab(SettingsTabId tab_id, const toml::value& data,
                                      const toml::value& gs_data, bool is_newly_created) {
    Q_UNUSED(is_newly_created);
    switch (tab_id) {
    case SettingsTabId::General: {
        ui->consoleLanguageComboBox->setCurrentIndex(
            std::distance(
                languageIndexes.begin(),
                std::find(languageIndexes.begin(), languageIndexes.end(),
                          toml::find_or<int>(gs_data, "Settings", "consoleLanguage", 6))) %
            languageIndexes.size());

        ui->showSplashCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "General", "showSplash", false));

        ui->horizontalVolumeSlider->setValue(
            toml::find_or<int>(gs_data, "General", "volumeSlider", 100));
        ui->volumeText->setText(QString::number(ui->horizontalVolumeSlider->sliderPosition()) +
                                "%");

        ui->GenAudioComboBox->setCurrentText(QString::fromStdString(
            toml::find_or<std::string>(gs_data, "Audio", "mainOutputDevice", "")));
        ui->DsAudioComboBox->setCurrentText(QString::fromStdString(
            toml::find_or<std::string>(gs_data, "Audio", "padSpkOutputDevice", "")));
        break;
    }
    case SettingsTabId::Gui: {
        if (!is_game_specific) {
            ui->emulatorLanguageComboBox->setCurrentIndex(
                languages[m_gui_settings->GetValue(gui::gen_guiLanguage).toString().toStdString()]);

            ui->playBGMCheckBox->setChecked(
                m_gui_settings->GetValue(gui::gl_playBackgroundMusic).toBool());

            ui->BGMVolumeSlider->setValue(
                m_gui_settings->GetValue(gui::gl_backgroundMusicVolume).toInt());
            ui->discordRPCCheckbox->setChecked(
                toml::find_or<bool>(data, "General", "enableDiscordRPC", true));

            ui->trophyKeyLineEdit->setText(
                QString::fromStdString(toml::find_or<std::string>(data, "Keys", "TrophyKey", "")));
            ui->trophyKeyLineEdit->setEchoMode(QLineEdit::Password);
            ui->enableCompatibilityCheckBox->setChecked(
                m_gui_settings->GetValue(gui::glc_showCompatibility).toBool());
            ui->checkCompatibilityOnStartupCheckBox->setChecked(
                m_gui_settings->GetValue(gui::gen_checkCompatibilityAtStartup).toBool());

            ui->backgroundImageOpacitySlider->setValue(
                m_gui_settings->GetValue(gui::gl_backgroundImageOpacity).toInt());
            ui->showBackgroundImageCheckBox->setChecked(
                m_gui_settings->GetValue(gui::gl_showBackgroundImage).toBool());

            backgroundImageOpacitySlider_backup =
                m_gui_settings->GetValue(gui::gl_backgroundImageOpacity).toInt();
            bgm_volume_backup = m_gui_settings->GetValue(gui::gl_backgroundMusicVolume).toInt();

#ifdef ENABLE_UPDATER
            ui->updateCheckBox->setChecked(
                m_gui_settings->GetValue(gui::gen_checkForUpdates).toBool());
            ui->changelogCheckBox->setChecked(
                m_gui_settings->GetValue(gui::gen_showChangeLog).toBool());
#endif
        }
        break;
    }
    case SettingsTabId::Graphics: {
        ui->graphicsAdapterBox->setCurrentIndex(toml::find_or<int>(gs_data, "Vulkan", "gpuId", -1) +
                                                1);
        ui->widthSpinBox->setValue(toml::find_or<int>(gs_data, "GPU", "screenWidth", 1280));
        ui->heightSpinBox->setValue(toml::find_or<int>(gs_data, "GPU", "screenHeight", 720));
        ui->dumpShadersCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "GPU", "dumpShaders", false));
        ui->nullGpuCheckBox->setChecked(toml::find_or<bool>(gs_data, "GPU", "nullGpu", false));
        ui->enableHDRCheckBox->setChecked(toml::find_or<bool>(gs_data, "GPU", "allowHDR", false));
        ui->FSRCheckBox->setChecked(toml::find_or<bool>(gs_data, "GPU", "fsrEnabled", true));
        ui->RCASCheckBox->setChecked(toml::find_or<bool>(gs_data, "GPU", "rcasEnabled", true));
        ui->RCASSlider->setValue(toml::find_or<int>(gs_data, "GPU", "rcasAttenuation", 250));
        ui->RCASValue->setText(QString::number(ui->RCASSlider->value() / 1000.0, 'f', 3));

        std::string fullScreenMode =
            toml::find_or<std::string>(gs_data, "GPU", "FullscreenMode", "Windowed");
        QString translatedText_FullscreenMode =
            screenModeMap.key(QString::fromStdString(fullScreenMode));
        ui->displayModeComboBox->setCurrentText(translatedText_FullscreenMode);

        std::string presentMode =
            toml::find_or<std::string>(gs_data, "GPU", "presentMode", "Mailbox");
        QString translatedText_PresentMode =
            presentModeMap.key(QString::fromStdString(presentMode));
        ui->presentModeComboBox->setCurrentText(translatedText_PresentMode);
        break;
    }
    case SettingsTabId::User: {
        ui->userNameLineEdit->setText(QString::fromStdString(
            toml::find_or<std::string>(gs_data, "General", "userName", "shadPS4")));
        ui->disableTrophycheckBox->setChecked(
            toml::find_or<bool>(gs_data, "General", "isTrophyPopupDisabled", false));
        ui->popUpDurationSpinBox->setValue(
            toml::find_or<double>(gs_data, "General", "trophyNotificationDuration", 6.0));

        std::string sideTrophy =
            toml::find_or<std::string>(gs_data, "General", "sideTrophy", "right");
        QString side = QString::fromStdString(sideTrophy);
        ui->radioButton_Left->setChecked(side == "left");
        ui->radioButton_Right->setChecked(side == "right");
        ui->radioButton_Top->setChecked(side == "top");
        ui->radioButton_Bottom->setChecked(side == "bottom");
        break;
    }
    case SettingsTabId::Input: {
        ui->hideCursorComboBox->setCurrentIndex(
            toml::find_or<int>(gs_data, "Input", "cursorState", 1));
        OnCursorStateChanged(toml::find_or<int>(gs_data, "Input", "cursorState", 1));
        ui->idleTimeoutSpinBox->setValue(
            toml::find_or<int>(gs_data, "Input", "cursorHideTimeout", 5));
        ui->motionControlsCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Input", "isMotionControlsEnabled", true));
        ui->backgroundControllerCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Input", "backgroundControllerInput", false));
        ui->usbComboBox->setCurrentIndex(
            toml::find_or<int>(gs_data, "Input", "usbDeviceBackend", 0));

        std::string micDevice =
            toml::find_or<std::string>(gs_data, "Audio", "micDevice", "Default Device");
        QString micValue = QString::fromStdString(micDevice);
        int micIndex = ui->micComboBox->findData(micValue);
        if (micIndex != -1) {
            ui->micComboBox->setCurrentIndex(micIndex);
        } else {
            ui->micComboBox->setCurrentIndex(0);
        }
        break;
    }
    case SettingsTabId::Paths: {
        if (!is_game_specific) {
            const auto save_data_path = Config::GetSaveDataPath();
            QString save_data_path_string;
            Common::FS::PathToQString(save_data_path_string, save_data_path);
            ui->currentSaveDataPath->setText(save_data_path_string);

            const auto dlc_folder_path = Config::getAddonInstallDir();
            QString dlc_folder_path_string;
            Common::FS::PathToQString(dlc_folder_path_string, dlc_folder_path);
            ui->currentDLCFolder->setText(dlc_folder_path_string);

            const auto sysmodules_path = Config::getSysModulesPath();
            QString sysmodules_path_string;
            Common::FS::PathToQString(sysmodules_path_string, sysmodules_path);
            ui->sysmodulesPath->setText(sysmodules_path_string);

            ui->removeFolderButton->setEnabled(
                !ui->gameFoldersListWidget->selectedItems().isEmpty());

            SyncGameFoldersFromConfig(data);
        }
        break;
    }
    case SettingsTabId::Log: {
        std::string logType = toml::find_or<std::string>(gs_data, "General", "logType", "sync");
        QString translatedText_logType = logTypeMap.key(QString::fromStdString(logType));
        if (!translatedText_logType.isEmpty()) {
            ui->logTypeComboBox->setCurrentText(translatedText_logType);
        }
        ui->logFilterLineEdit->setText(QString::fromStdString(
            toml::find_or<std::string>(gs_data, "General", "logFilter", "")));
        ui->enableLoggingCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Debug", "logEnabled", true));
        ui->separateLogFilesCheckbox->setChecked(
            toml::find_or<bool>(gs_data, "Debug", "isSeparateLogFilesEnabled", false));
        break;
    }
    case SettingsTabId::Debug: {
        ui->debugDump->setChecked(toml::find_or<bool>(gs_data, "Debug", "DebugDump", false));
        ui->vkValidationCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "validation", false));
        ui->vkSyncValidationCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "validation_sync", false));
        ui->vkCoreValidationCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "validation_core", false));
        ui->vkGpuValidationCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "validation_gpu", false));
        ui->vkValidationCheckBox->isChecked() ? ui->vkLayersGroupBox->setVisible(true)
                                              : ui->vkLayersGroupBox->setVisible(false);

        ui->rdocCheckBox->setChecked(toml::find_or<bool>(gs_data, "Vulkan", "rdocEnable", false));
        ui->crashDiagnosticsCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "crashDiagnostic", false));
        ui->guestMarkersCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "guestMarkers", false));
        ui->hostMarkersCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "hostMarkers", false));
        ui->copyGPUBuffersCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "GPU", "copyGPUBuffers", false));
        ui->collectShaderCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Debug", "CollectShader", false));
        break;
    }
    case SettingsTabId::Experimental: {
        ui->readbacksCheckBox->setChecked(toml::find_or<bool>(gs_data, "GPU", "readbacks", false));
        ui->readbackLinearImagesCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "GPU", "readbackLinearImages", false));
        ui->dmaCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "GPU", "directMemoryAccess", false));
        ui->neoCheckBox->setChecked(toml::find_or<bool>(gs_data, "General", "isPS4Pro", false));
        ui->devkitCheckBox->setChecked(toml::find_or<bool>(gs_data, "General", "isDevKit", false));
        ui->networkConnectedCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "General", "isConnectedToNetwork", false));
        ui->shaderCaheCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "pipelineCacheEnable", false));
        ui->shaderCacheArchiveCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "Vulkan", "pipelineCacheArchive", false));
        ui->psnSignInCheckBox->setChecked(
            toml::find_or<bool>(gs_data, "General", "isPSNSignedIn", false));
        ui->vblankSpinBox->setValue(toml::find_or<int>(gs_data, "GPU", "vblankFrequency", 60));
        ui->dmemSpinBox->setValue(toml::find_or<int>(gs_data, "General", "extraDmemInMbytes", 0));

        ui->shaderCaheCheckBox->isChecked() ? ui->shaderCacheArchiveCheckBox->setVisible(true)
                                            : ui->shaderCacheArchiveCheckBox->setVisible(false);
        break;
    }
    case SettingsTabId::Unknown:
        break;
    }
}

void SettingsDialog::SetDefaultValuesForScope() {
    if (scoped_tab_id == SettingsTabId::Unknown) {
        SetDefaultValuesForTab(SettingsTabId::General);
        SetDefaultValuesForTab(SettingsTabId::Gui);
        SetDefaultValuesForTab(SettingsTabId::Graphics);
        SetDefaultValuesForTab(SettingsTabId::User);
        SetDefaultValuesForTab(SettingsTabId::Input);
        SetDefaultValuesForTab(SettingsTabId::Paths);
        SetDefaultValuesForTab(SettingsTabId::Log);
        SetDefaultValuesForTab(SettingsTabId::Debug);
        SetDefaultValuesForTab(SettingsTabId::Experimental);
        return;
    }

    SetDefaultValuesForTab(scoped_tab_id);
}

void SettingsDialog::SetDefaultValuesForTab(SettingsTabId tab_id) {
    const bool is_specific = is_game_specific;

    switch (tab_id) {
    case SettingsTabId::General:
        Config::setVolumeSlider(100, is_specific);
        Config::setLanguage(1, is_specific);
        Config::setShowSplash(false, is_specific);
        Config::setMainOutputDevice("Default Device", is_specific);
        Config::setPadSpkOutputDevice("Default Device", is_specific);
        break;
    case SettingsTabId::Gui:
        if (!is_specific) {
            m_gui_settings->SetValue(gui::gl_showBackgroundImage, true);
            m_gui_settings->SetValue(gui::gl_backgroundImageOpacity, 50);
            m_gui_settings->SetValue(gui::gl_playBackgroundMusic, false);
            m_gui_settings->SetValue(gui::gl_backgroundMusicVolume, 50);
            m_gui_settings->SetValue(gui::gen_checkForUpdates, false);
            m_gui_settings->SetValue(gui::gen_showChangeLog, false);
            m_gui_settings->SetValue(gui::gen_guiLanguage, "en_US");
            m_gui_settings->SetValue(gui::glc_showLoadGameSizeEnabled, true);
            m_gui_settings->SetValue(gui::glc_showCompatibility, false);
            m_gui_settings->SetValue(gui::gen_checkCompatibilityAtStartup, false);
            m_gui_settings->SetValue(gui::gen_homeTab, "General");

            Config::setEnableDiscordRPC(false);
            Config::setTrophyKey("");
        }
        break;
    case SettingsTabId::Graphics:
        Config::setWindowWidth(1280, is_specific);
        Config::setWindowHeight(720, is_specific);
        Config::setIsFullscreen(false, is_specific);
        Config::setFullscreenMode("Windowed", is_specific);
        Config::setPresentMode("Mailbox", is_specific);
        Config::setAllowHDR(false, is_specific);
        Config::setGpuId(-1, is_specific);
        Config::setDumpShaders(false, is_specific);
        Config::setNullGpu(false, is_specific);
        Config::setFsrEnabled(true, is_specific);
        Config::setRcasEnabled(true, is_specific);
        Config::setRcasAttenuation(250, is_specific);
        break;
    case SettingsTabId::User:
        Config::setUserName("shadPS4", is_specific);
        Config::setisTrophyPopupDisabled(false, is_specific);
        Config::setTrophyNotificationDuration(6.0, is_specific);
        Config::setSideTrophy("right", is_specific);
        break;
    case SettingsTabId::Input:
        Config::setCursorState(Config::HideCursorState::Idle, is_specific);
        Config::setCursorHideTimeout(5, is_specific);
        Config::setIsMotionControlsEnabled(true, is_specific);
        Config::setBackgroundControllerInput(false, is_specific);
        Config::setUsbDeviceBackend(Config::UsbBackendType::Real, is_specific);
        Config::setMicDevice("Default Device", is_specific);
        break;
    case SettingsTabId::Paths:
        break;
    case SettingsTabId::Log:
        Config::setLoggingEnabled(true, is_specific);
        Config::setLogType("sync", is_specific);
        Config::setLogFilter("", is_specific);
        Config::setSeparateLogFilesEnabled(false, is_specific);
        break;
    case SettingsTabId::Debug:
        Config::setDebugDump(false, is_specific);
        Config::setVkValidation(false, is_specific);
        Config::setVkSyncValidation(false, is_specific);
        Config::setVkCoreValidation(true, is_specific);
        Config::setVkGpuValidation(false, is_specific);
        Config::setRdocEnabled(false, is_specific);
        Config::setVkHostMarkersEnabled(false, is_specific);
        Config::setVkGuestMarkersEnabled(false, is_specific);
        Config::setVkCrashDiagnosticEnabled(false, is_specific);
        Config::setCollectShaderForDebug(false, is_specific);
        Config::setCopyGPUCmdBuffers(false, is_specific);
        break;
    case SettingsTabId::Experimental:
        Config::setReadbacks(false, is_specific);
        Config::setReadbackLinearImages(false, is_specific);
        Config::setDirectMemoryAccess(false, is_specific);
        Config::setDevKitConsole(false, is_specific);
        Config::setNeoMode(false, is_specific);
        Config::setConnectedToNetwork(false, is_specific);
        Config::setPipelineCacheEnabled(false, is_specific);
        Config::setPipelineCacheArchived(false, is_specific);
        Config::setPSNSignedIn(false, is_specific);
        Config::setVblankFreq(60, is_specific);
        Config::setExtraDmemInMbytes(0, is_specific);
        break;
    case SettingsTabId::Unknown:
        break;
    }
}

void SettingsDialog::SyncRealTimeWidgetsForScope(const toml::value& data,
                                                 const toml::value& gs_data) {
    if (scoped_tab_id == SettingsTabId::Unknown) {
        SyncRealTimeWidgetsForTab(SettingsTabId::General, data, gs_data);
        SyncRealTimeWidgetsForTab(SettingsTabId::Paths, data, gs_data);
        SyncRealTimeWidgetsForTab(SettingsTabId::Graphics, data, gs_data);
        return;
    }

    SyncRealTimeWidgetsForTab(scoped_tab_id, data, gs_data);
}

void SettingsDialog::SyncRealTimeWidgetsForTab(SettingsTabId tab_id, const toml::value& data,
                                               const toml::value& gs_data) {
    switch (tab_id) {
    case SettingsTabId::General:
        SyncVolumeFromConfig(gs_data);
        break;
    case SettingsTabId::Paths:
        SyncGameFoldersFromConfig(data);
        break;
    case SettingsTabId::Graphics:
        SyncRunningGameGpuFromConfig(gs_data);
        break;
    case SettingsTabId::Gui:
    case SettingsTabId::User:
    case SettingsTabId::Input:
    case SettingsTabId::Log:
    case SettingsTabId::Debug:
    case SettingsTabId::Experimental:
    case SettingsTabId::Unknown:
        break;
    }
}

void SettingsDialog::SyncGameFoldersFromConfig(const toml::value& data) {
    if (is_game_specific) {
        return;
    }

    ui->gameFoldersListWidget->clear();
    if (!data.contains("GUI")) {
        return;
    }

    const toml::value& gui = data.at("GUI");
    const auto install_dir_array =
        toml::find_or<std::vector<std::u8string>>(gui, "installDirs", {});

    std::vector<bool> install_dirs_enabled;
    try {
        install_dirs_enabled = Config::getGameInstallDirsEnabled();
    } catch (...) {
        install_dirs_enabled.resize(install_dir_array.size(), true);
    }

    if (install_dirs_enabled.size() < install_dir_array.size()) {
        install_dirs_enabled.resize(install_dir_array.size(), true);
    }

    std::vector<Config::GameInstallDir> settings_install_dirs_config;

    for (size_t i = 0; i < install_dir_array.size(); i++) {
        std::filesystem::path dir = install_dir_array[i];
        bool enabled = install_dirs_enabled[i];

        settings_install_dirs_config.push_back({dir, enabled});

        QString path_string;
        Common::FS::PathToQString(path_string, dir);

        QListWidgetItem* item = new QListWidgetItem(path_string);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        ui->gameFoldersListWidget->addItem(item);
    }

    Config::setAllGameInstallDirs(settings_install_dirs_config);
}

void SettingsDialog::SyncVolumeFromConfig(const toml::value& gs_data) {
    int sliderValue = toml::find_or<int>(gs_data, "General", "volumeSlider", 100);
    ui->horizontalVolumeSlider->setValue(sliderValue);

    // Since config::set can be called for volume slider (connected to the widget) outside the save
    // function, need to null it out if GS GUI is closed without saving
    is_game_specific ? Config::resetGameSpecificValue("volumeSlider")
                     : Config::setVolumeSlider(sliderValue);
}

void SettingsDialog::SyncRunningGameGpuFromConfig(const toml::value& gs_data) {
    if (!EmulatorState::GetInstance()->IsGameRunning()) {
        return;
    }

    m_ipc_client->setFsr(toml::find_or<bool>(gs_data, "GPU", "fsrEnabled", true));
    m_ipc_client->setRcas(toml::find_or<bool>(gs_data, "GPU", "rcasEnabled", true));
    m_ipc_client->setRcasAttenuation(toml::find_or<int>(gs_data, "GPU", "rcasAttenuation", 250));
}

bool SettingsDialog::LoadSyncConfigData(toml::value& data, toml::value& gs_data) const {
    std::filesystem::path userdir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    try {
        data = toml::parse(userdir / "config.toml");
    } catch (std::exception& ex) {
        fmt::print("Got exception trying to load config file. Exception: {}\n", ex.what());
        return false;
    }

    try {
        gs_data = is_game_specific
                      ? toml::parse(Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) /
                                    (gs_serial + ".toml"))
                      : data;
    } catch (std::exception& ex) {
        fmt::print("Got exception trying to load config file. Exception: {}\n", ex.what());
        return false;
    }

    return true;
}

void SettingsDialog::pollSDLevents() {
    SDL_Event event;
    while (SdlEventWrapper::Wrapper::wrapperActive) {

        if (!SDL_WaitEvent(&event)) {
            return;
        }

        if (event.type == SDL_EVENT_QUIT) {
            return;
        }

        if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED) {
            onAudioDeviceChange(true);
        }

        if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) {
            onAudioDeviceChange(false);
        }
    }
}

void SettingsDialog::onAudioDeviceChange(bool isAdd) {
    ui->GenAudioComboBox->clear();
    ui->DsAudioComboBox->clear();

    // prevent device list from refreshing too fast
    if (!isAdd)
        QThread::msleep(100);

    int deviceCount;
    QStringList deviceList;
    SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&deviceCount);

    if (!devices) {
        LOG_ERROR(Lib_AudioOut, "Unable to retrieve audio device list {}", SDL_GetError());
        return;
    }

    for (int i = 0; i < deviceCount; ++i) {
        const char* name = SDL_GetAudioDeviceName(devices[i]);
        std::string name_string = std::string(name);
        deviceList.append(QString::fromStdString(name_string));
    }

    ui->GenAudioComboBox->addItem(tr("Default Device"), "Default Device");
    ui->GenAudioComboBox->addItems(deviceList);
    ui->GenAudioComboBox->setCurrentText(QString::fromStdString(Config::getMainOutputDevice()));

    ui->DsAudioComboBox->addItem(tr("Default Device"), "Default Device");
    ui->DsAudioComboBox->addItems(deviceList);
    ui->DsAudioComboBox->setCurrentText(QString::fromStdString(Config::getPadSpkOutputDevice()));

    SDL_free(devices);
}

void SettingsDialog::getPhysicalDevices() {
    if (volkInitialize() != VK_SUCCESS) {
        qWarning() << "Failed to initialize Volk.";
        return;
    }

    // Create Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "shadPS4QtLauncher";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

#ifdef __APPLE__
    const char* portabilityExtensionName = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    const size_t portabilityExtensionNameLength = strlen(portabilityExtensionName);

    uint32_t instanceExtensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);

    if (instanceExtensionCount != 0) {
        std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount,
                                               instanceExtensions.data());

        const auto portabilityExtension = std::ranges::find_if(
            instanceExtensions, [&portabilityExtensionName, &portabilityExtensionNameLength](
                                    const VkExtensionProperties& ext) {
                return strncmp(ext.extensionName, portabilityExtensionName,
                               portabilityExtensionNameLength) == 0;
            });

        if (portabilityExtension != instanceExtensions.end()) {
            instInfo.ppEnabledExtensionNames = &portabilityExtensionName;
            instInfo.enabledExtensionCount = 1;
            instInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    }
#endif

    VkInstance instance;
    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS) {
        qWarning() << "Failed to create Vulkan instance.";
        return;
    }

    // Load instance-based function pointers
    volkLoadInstance(instance);

    // Enumerate devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        qWarning() << "No Vulkan physical devices found.";
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    m_physical_devices.clear();
    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        QString name = QString::fromUtf8(props.deviceName, -1);
        m_physical_devices.push_back(name);
    }

    vkDestroyInstance(instance, nullptr);
}
