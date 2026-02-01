// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDockWidget>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QStatusBar>

#include "about_dialog.h"
#include "cheats_patches.h"
#include "version_dialog.h"
#ifdef ENABLE_UPDATER
#include "check_update.h"
#endif
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "common/scm_rev.h"
#include "common/versions.h"
#include "control_settings.h"
#include "core/emulator_state.h"
#include "dimensions_dialog.h"
#include "game_install_dialog.h"
#include "hotkeys.h"
#include "infinity_dialog.h"
#include "input/controller.h"
#include "ipc/ipc_client.h"
#include "kbm_gui.h"
#include "main_window.h"
#include "open_targets.h"
#include "settings_dialog.h"
#include "skylander_dialog.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    installEventFilter(this);
    setAttribute(Qt::WA_DeleteOnClose);
    m_gui_settings = std::make_shared<gui_settings>();
    ui->toggleLabelsAct->setChecked(
        m_gui_settings->GetValue(gui::mw_showLabelsUnderIcons).toBool());

    m_ipc_client->gameClosedFunc = [this]() { onGameClosed(); };
    m_ipc_client->restartEmulatorFunc = [this]() { RestartEmulator(); };
    m_ipc_client->startGameFunc = [this]() { RunGame(); };
}

MainWindow::~MainWindow() {
    SaveWindowState();
}

bool MainWindow::Init(InitMode init_mode) {
    auto start = std::chrono::steady_clock::now();
    // setup ui
    LoadTranslation();
    AddUiWidgets();
    CreateActions();
    CreateRecentGameActions();
    ConfigureGuiFromSettings();
    CreateDockWindows(true);
    CreateConnects();
    SetLastUsedTheme();
    SetLastIconSizeBullet();
    const bool is_full_init = init_mode == InitMode::Full;
    if (is_full_init) {
        setMinimumSize(900, 405);
        std::string window_title = "";
        std::string remote_url(Common::g_scm_remote_url);
        std::string remote_host = Common::GetRemoteNameFromLink();
        if (remote_host == "shadps4-emu" || remote_url.length() == 0) {
            window_title = fmt::format("shadPS4QtLauncher v{} {} {}", Common::g_scm_app_version,
                                       Common::g_scm_branch, Common::g_scm_desc);
        } else {
            window_title = fmt::format("shadPS4QtLauncher v{} {}/{} {}", Common::g_scm_app_version,
                                       remote_host, Common::g_scm_branch, Common::g_scm_desc);
        }
        setWindowTitle(QString::fromStdString(window_title));
    }
    // load game list
    LoadGameLists();

#ifdef ENABLE_UPDATER
    // Check for update
    if (is_full_init) {
        CheckUpdateMain(true);
    }
#endif

    LoadVersionComboBox();
    if (is_full_init && m_gui_settings->GetValue(gui::vm_checkOnStartup).toBool()) {
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        versionDialog->checkUpdatePre(false);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    statusBar.reset(new QStatusBar);
    this->setStatusBar(statusBar.data());
    // Update status bar
    int numGames = m_game_info->m_games.size();
    QString statusMessage = tr("Games: ") + QString::number(numGames) + " (" +
                            QString::number(duration.count()) + "ms)";
    statusBar->showMessage(statusMessage);

    return true;
}

UiOpenTargets::OpenTargetContext MainWindow::BuildOpenTargetContext(
    QWidget* parent, bool attach_parent_destroy, bool is_game_specific,
    const std::string& game_serial) const {
    UiOpenTargets::OpenTargetContext context{};
    context.gui_settings_shared = m_gui_settings;
    context.compat_info = m_compat_info;
    context.ipc_client = m_ipc_client;
    context.game_info = m_game_info;
    context.running_game_serial = runningGameSerial;
    context.is_game_running = EmulatorState::GetInstance()->IsGameRunning();
    context.is_game_specific = is_game_specific;
    context.game_serial = game_serial;
    context.attach_parent_destroy = attach_parent_destroy;
    context.parent = parent;
    return context;
}

const std::string& MainWindow::GetRunningGameSerial() const {
    return runningGameSerial;
}

void MainWindow::CreateActions() {
    // create action group for icon size
    m_icon_size_act_group = new QActionGroup(this);
    m_icon_size_act_group->addAction(ui->setIconSizeTinyAct);
    m_icon_size_act_group->addAction(ui->setIconSizeSmallAct);
    m_icon_size_act_group->addAction(ui->setIconSizeMediumAct);
    m_icon_size_act_group->addAction(ui->setIconSizeLargeAct);

    // create action group for list mode
    m_list_mode_act_group = new QActionGroup(this);
    m_list_mode_act_group->addAction(ui->setlistModeListAct);
    m_list_mode_act_group->addAction(ui->setlistModeGridAct);
    m_list_mode_act_group->addAction(ui->setlistElfAct);

    // create action group for themes
    m_theme_act_group = new QActionGroup(this);
    m_theme_act_group->addAction(ui->setThemeDark);
    m_theme_act_group->addAction(ui->setThemeLight);
    m_theme_act_group->addAction(ui->setThemeGreen);
    m_theme_act_group->addAction(ui->setThemeBlue);
    m_theme_act_group->addAction(ui->setThemeViolet);
    m_theme_act_group->addAction(ui->setThemeGruvbox);
    m_theme_act_group->addAction(ui->setThemeTokyoNight);
    m_theme_act_group->addAction(ui->setThemeOled);
}

void MainWindow::PauseGame() {
    if (is_paused) {
        m_ipc_client->resumeGame();
        is_paused = false;
    } else {
        m_ipc_client->pauseGame();
        is_paused = true;
    }
}

void MainWindow::StopGame() {
    m_ipc_client->stopEmulator();
}

void MainWindow::onGameClosed() {
    EmulatorState::GetInstance()->SetGameRunning(false);
    is_paused = false;

    // swap the pause button back to the play button on close
    ui->playButton->setVisible(true);
    ui->pauseButton->setVisible(false);
    if (ui->toggleLabelsAct->isChecked()) {
        QLabel* playButtonLabel = ui->playButton->parentWidget()->findChild<QLabel*>();
        if (playButtonLabel)
            playButtonLabel->setVisible(true);

        QLabel* pauseButtonLabel = ui->pauseButton->parentWidget()->findChild<QLabel*>();
        if (pauseButtonLabel)
            pauseButtonLabel->setVisible(false);
    }

    // clear dialogs when game closed
    skylander_dialog* sky_diag = skylander_dialog::get_dlg(this, m_ipc_client);
    sky_diag->clear_all();
    dimensions_dialog* dim_diag = dimensions_dialog::get_dlg(this, m_ipc_client);
    dim_diag->clear_all();
    infinity_dialog* inf_diag = infinity_dialog::get_dlg(this, m_ipc_client);
    inf_diag->clear_all();
}

void MainWindow::RestartGame() {
    m_ipc_client->restartEmulator();
}

void MainWindow::toggleLabelsUnderIcons() {
    bool showLabels = ui->toggleLabelsAct->isChecked();
    m_gui_settings->SetValue(gui::mw_showLabelsUnderIcons, showLabels);
    UpdateToolbarLabels();
    if (EmulatorState::GetInstance()->IsGameRunning()) {
        UpdateToolbarButtons();
    }
}

void MainWindow::toggleFullscreen() {
    m_ipc_client->toggleFullscreen();
}

QWidget* MainWindow::createButtonWithLabel(QPushButton* button, const QString& labelText,
                                           bool showLabel) {
    QWidget* container = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setAlignment(Qt::AlignCenter | Qt::AlignBottom);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(button);

    QLabel* label = nullptr;
    if (showLabel && ui->toggleLabelsAct->isChecked()) {
        label = new QLabel(labelText, this);
        label->setAlignment(Qt::AlignCenter | Qt::AlignBottom);
        layout->addWidget(label);
        button->setToolTip("");
    } else {
        button->setToolTip(labelText);
    }

    container->setLayout(layout);
    container->setProperty("buttonLabel", QVariant::fromValue(label));
    return container;
}

QWidget* createSpacer(QWidget* parent) {
    QWidget* spacer = new QWidget(parent);
    spacer->setFixedWidth(15);
    spacer->setFixedHeight(15);
    return spacer;
}

void MainWindow::AddUiWidgets() {
    // add toolbar widgets
    QApplication::setStyle("Fusion");

    bool showLabels = ui->toggleLabelsAct->isChecked();
    ui->toolBar->clear();

    ui->toolBar->addWidget(createSpacer(this));
    ui->toolBar->addWidget(createButtonWithLabel(ui->playButton, tr("Play"), showLabels));
    ui->toolBar->addWidget(createButtonWithLabel(ui->pauseButton, tr("Pause"), showLabels));
    ui->toolBar->addWidget(createButtonWithLabel(ui->stopButton, tr("Stop"), showLabels));
    ui->toolBar->addWidget(createButtonWithLabel(ui->restartButton, tr("Restart"), showLabels));
    ui->toolBar->addWidget(createSpacer(this));
    ui->toolBar->addWidget(createButtonWithLabel(ui->settingsButton, tr("Settings"), showLabels));
    ui->toolBar->addWidget(
        createButtonWithLabel(ui->fullscreenButton, tr("Full Screen"), showLabels));
    ui->toolBar->addWidget(createSpacer(this));
    ui->toolBar->addWidget(
        createButtonWithLabel(ui->controllerButton, tr("Controllers"), showLabels));
    ui->toolBar->addWidget(createButtonWithLabel(ui->keyboardButton, tr("Keyboard"), showLabels));
    ui->toolBar->addWidget(createSpacer(this));
    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setMinimumWidth(2);
    ui->toolBar->addWidget(line);
    ui->toolBar->addWidget(createSpacer(this));
    if (showLabels) {
        QLabel* pauseButtonLabel = ui->pauseButton->parentWidget()->findChild<QLabel*>();
        if (pauseButtonLabel) {
            pauseButtonLabel->setVisible(false);
        }
    }
    ui->toolBar->addWidget(
        createButtonWithLabel(ui->refreshButton, tr("Refresh List"), showLabels));
    ui->toolBar->addWidget(createSpacer(this));

    QBoxLayout* toolbarLayout = new QBoxLayout(QBoxLayout::TopToBottom);
    toolbarLayout->setSpacing(2);
    toolbarLayout->setContentsMargins(2, 2, 2, 2);
    ui->sizeSliderContainer->setFixedWidth(150);

    QWidget* searchSliderContainer = new QWidget(this);
    QBoxLayout* searchSliderLayout = new QBoxLayout(QBoxLayout::TopToBottom);
    searchSliderLayout->setContentsMargins(0, 0, 6, 6);
    searchSliderLayout->setSpacing(2);
    ui->mw_searchbar->setFixedWidth(150);

    searchSliderLayout->addWidget(ui->sizeSliderContainer);
    searchSliderLayout->addWidget(ui->mw_searchbar);

    searchSliderContainer->setLayout(searchSliderLayout);

    ui->toolBar->addWidget(searchSliderContainer);

    if (!showLabels) {
        toolbarLayout->addWidget(searchSliderContainer);
    }

    ui->playButton->setVisible(true);
    ui->pauseButton->setVisible(false);

    // Expandable spacer to push elements to the right (Version Manager)
    QWidget* expandingSpacer = new QWidget(this);
    expandingSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->toolBar->addWidget(expandingSpacer);
    QWidget* versionContainer = new QWidget(this);
    QVBoxLayout* versionLayout = new QVBoxLayout(versionContainer);
    versionLayout->setContentsMargins(0, 0, 0, 0);
    versionLayout->addWidget(ui->versionComboBox);
    versionLayout->addWidget(ui->versionManagerButton);
    ui->versionManagerButton->setText(tr("Version Manager"));
    ui->toolBar->addWidget(versionContainer);
}

void MainWindow::UpdateToolbarButtons() {
    // add toolbar widgets when game is running
    bool showLabels = ui->toggleLabelsAct->isChecked();

    ui->playButton->setVisible(false);
    ui->pauseButton->setVisible(true);

    if (showLabels) {
        QLabel* playButtonLabel = ui->playButton->parentWidget()->findChild<QLabel*>();
        if (playButtonLabel)
            playButtonLabel->setVisible(false);
    }

    if (is_paused) {
        ui->pauseButton->setIcon(ui->playButton->icon());
        ui->pauseButton->setToolTip(tr("Resume"));
    } else {
        if (isIconBlack) {
            ui->pauseButton->setIcon(QIcon(":images/pause_icon.png"));
        } else {
            ui->pauseButton->setIcon(RecolorIcon(QIcon(":images/pause_icon.png"), isWhite));
        }
        ui->pauseButton->setToolTip(tr("Pause"));
    }

    if (showLabels) {
        QLabel* pauseButtonLabel = ui->pauseButton->parentWidget()->findChild<QLabel*>();
        if (pauseButtonLabel) {
            pauseButtonLabel->setText(is_paused ? tr("Resume") : tr("Pause"));
            pauseButtonLabel->setVisible(true);
        }
    }
}

void MainWindow::UpdateToolbarLabels() {
    AddUiWidgets();
}

void MainWindow::CreateDockWindows(bool newDock) {
    // place holder widget is needed for good health they say :)
    QWidget* phCentralWidget = new QWidget(this);
    setCentralWidget(phCentralWidget);

    QWidget* dockContents = new QWidget(this);
    QVBoxLayout* dockLayout = new QVBoxLayout(this);

    ui->splitter = new QSplitter(Qt::Vertical);
    ui->logDisplay = new QTextEdit(ui->splitter);
    ui->logDisplay->setText(tr("Game Log"));
    ui->logDisplay->setReadOnly(true);

    if (newDock) {
        m_dock_widget.reset(new QDockWidget(tr("Game List"), this));
        m_game_list_frame.reset(
            new GameListFrame(m_gui_settings, m_game_info, m_compat_info, m_ipc_client, this));
        m_game_list_frame->setObjectName("gamelist");
        m_game_grid_frame.reset(
            new GameGridFrame(m_gui_settings, m_game_info, m_compat_info, m_ipc_client, this));
        m_game_grid_frame->setObjectName("gamegridlist");
        m_elf_viewer.reset(new ElfViewer(m_gui_settings, this));
        m_elf_viewer->setObjectName("elflist");
    }

    int table_mode = m_gui_settings->GetValue(gui::gl_mode).toInt();
    int slider_pos = 0;
    if (table_mode == 0) { // List
        m_game_grid_frame->hide();
        m_elf_viewer->hide();
        m_game_list_frame->show();
        if (!newDock) {
            m_game_list_frame->clearContents();
            m_game_list_frame->PopulateGameList();
        }
        ui->splitter->addWidget(m_game_list_frame.data());
        slider_pos = m_gui_settings->GetValue(gui::gl_slider_pos).toInt();
        ui->sizeSlider->setSliderPosition(slider_pos); // set slider pos at start;
        isTableList = true;
    } else if (table_mode == 1) { // Grid
        m_game_list_frame->hide();
        m_elf_viewer->hide();
        m_game_grid_frame->show();
        if (!newDock) {
            if (m_game_grid_frame->item(0, 0) == nullptr) {
                m_game_grid_frame->clearContents();
                m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
            }
        }
        ui->splitter->addWidget(m_game_grid_frame.data());
        slider_pos = m_gui_settings->GetValue(gui::gg_slider_pos).toInt();
        ui->sizeSlider->setSliderPosition(slider_pos); // set slider pos at start;
        isTableList = false;
    } else {
        m_game_list_frame->hide();
        m_game_grid_frame->hide();
        m_elf_viewer->show();
        ui->splitter->addWidget(m_elf_viewer.data());
        isTableList = false;
    }

    QPalette logPalette = ui->logDisplay->palette();
    logPalette.setColor(QPalette::Base, Qt::black);
    ui->logDisplay->setPalette(logPalette);
    ui->splitter->addWidget(ui->logDisplay);

    QList<int> defaultSizes = {800, 200}; // these are proportionally adjusted by qt
    QList<int> sizes = gui_settings::Var2IntList(m_gui_settings->GetValue(
        gui::main_window, "dockWidgetSizes", QVariant::fromValue(defaultSizes)));
    if (sizes[1] == 0) { // This happens if log is hidden when settings are saved
        sizes = defaultSizes;
    }

    ui->splitter->setSizes({sizes});
    ui->splitter->setCollapsible(0, false);
    ui->splitter->setCollapsible(1, false);

    bool showLog = ui->showLogAct->isChecked();
    showLog ? ui->logDisplay->show() : ui->logDisplay->hide();

    dockLayout->addWidget(ui->splitter);
    dockContents->setLayout(dockLayout);
    m_dock_widget->setWidget(dockContents);

    m_dock_widget->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_dock_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_dock_widget->resize(this->width(), this->height());

    addDockWidget(Qt::LeftDockWidgetArea, m_dock_widget.data());
    this->setDockNestingEnabled(true);
}

void MainWindow::LoadGameLists() {
    // Load compatibility database
    m_compat_info->LoadCompatibilityFile();

    // Update compatibility database
    if (m_gui_settings->GetValue(gui::gen_checkCompatibilityAtStartup).toBool())
        m_compat_info->UpdateCompatibilityDatabase(this);

    // Get game info from game folders.
    m_game_info->GetGameInfo(this);
    if (isTableList) {
        m_game_list_frame->PopulateGameList();
    } else {
        m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
    }
}

#ifdef ENABLE_UPDATER
void MainWindow::CheckUpdateMain(bool checkSave) {
    if (checkSave) {
        if (!m_gui_settings->GetValue(gui::gen_checkForUpdates).toBool()) {
            return;
        }
    }
    auto checkUpdate = new CheckUpdate(m_gui_settings, false);
    checkUpdate->exec();
}
#endif

void MainWindow::CreateConnects() {
    connect(this, &MainWindow::WindowResized, this, &MainWindow::HandleResize);
    connect(ui->mw_searchbar, &QLineEdit::textChanged, this, &MainWindow::SearchGameTable);
    connect(ui->exitAct, &QAction::triggered, this, &QWidget::close);
    connect(ui->refreshGameListAct, &QAction::triggered, this, &MainWindow::RefreshGameTable);
    connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::RefreshGameTable);
    connect(ui->showGameListAct, &QAction::triggered, this, &MainWindow::ShowGameList);
    connect(ui->toggleLabelsAct, &QAction::toggled, this, &MainWindow::toggleLabelsUnderIcons);
    connect(ui->fullscreenButton, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);

    const auto open_target = [this](UiOpenTargets::TargetId target_id) {
        auto context = BuildOpenTargetContext(this);
        return UiOpenTargets::OpenTarget(target_id, context, UiOpenTargets::OpenBehaviorForUi());
    };

    connect(ui->showLogAct, &QAction::triggered, this, [this](bool state) {
        if (state) {
            ui->logDisplay->show();
            m_gui_settings->SetValue(gui::mw_showLog, true);
        } else {
            ui->logDisplay->hide();
            m_gui_settings->SetValue(gui::mw_showLog, false);
        }
    });

    connect(ui->sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (isTableList) {
            m_game_list_frame->icon_size =
                48 + value; // 48 is the minimum icon size to use due to text disappearing.
            m_game_list_frame->ResizeIcons(48 + value);
            m_gui_settings->SetValue(gui::gl_icon_size, 48 + value);
            m_gui_settings->SetValue(gui::gl_slider_pos, value);
        } else {
            m_game_grid_frame->icon_size = 69 + value;
            m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
            m_gui_settings->SetValue(gui::gg_icon_size, 69 + value);
            m_gui_settings->SetValue(gui::gg_slider_pos, value);
        }
    });

    connect(ui->shadFolderAct, &QAction::triggered, this, [this]() {
        QString userPath;
        Common::FS::PathToQString(userPath, Common::FS::GetUserPath(Common::FS::PathType::UserDir));
        QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
    });

    connect(ui->playButton, &QPushButton::clicked, this, &MainWindow::StartGame);
    connect(ui->pauseButton, &QPushButton::clicked, this, &MainWindow::PauseGame);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::StopGame);
    connect(ui->restartButton, &QPushButton::clicked, this, &MainWindow::RestartGame);
    connect(m_game_grid_frame.get(), &QTableWidget::cellDoubleClicked, this,
            &MainWindow::StartGame);
    connect(m_game_list_frame.get(), &QTableWidget::cellDoubleClicked, this,
            &MainWindow::StartGame);

    const auto open_settings_dialog = [this, open_target]() {
        auto result = open_target(UiOpenTargets::TargetId::Settings);
        auto settings_dialog = qobject_cast<SettingsDialog*>(result.opened_window);
        if (!settings_dialog) {
            return;
        }

        connect(settings_dialog, &SettingsDialog::LanguageChanged, this,
                &MainWindow::OnLanguageChanged);

        connect(settings_dialog, &SettingsDialog::CompatibilityChanged, this,
                &MainWindow::RefreshGameTable);

        connect(settings_dialog, &SettingsDialog::accepted, this, &MainWindow::RefreshGameTable);
        connect(settings_dialog, &SettingsDialog::rejected, this, &MainWindow::RefreshGameTable);
        connect(settings_dialog, &SettingsDialog::close, this, &MainWindow::RefreshGameTable);

        connect(settings_dialog, &SettingsDialog::BackgroundOpacityChanged, this,
                [this](int opacity) {
                    m_gui_settings->SetValue(gui::gl_backgroundImageOpacity,
                                             std::clamp(opacity, 0, 100));
                    if (m_game_list_frame) {
                        QTableWidgetItem* current = m_game_list_frame->GetCurrentItem();
                        if (current) {
                            m_game_list_frame->SetListBackgroundImage(current);
                        }
                    }
                    if (m_game_grid_frame) {
                        if (m_game_grid_frame->IsValidCellSelected()) {
                            m_game_grid_frame->SetGridBackgroundImage(m_game_grid_frame->crtRow,
                                                                      m_game_grid_frame->crtColumn);
                        }
                    }
                });
    };

    connect(ui->configureAct, &QAction::triggered, this,
            [open_settings_dialog]() { open_settings_dialog(); });

    connect(ui->settingsButton, &QPushButton::clicked, this,
            [open_settings_dialog]() { open_settings_dialog(); });

    connect(ui->controllerButton, &QPushButton::clicked, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::Controllers); });

    connect(ui->keyboardButton, &QPushButton::clicked, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::KeyboardMouse); });

    connect(ui->versionManagerButton, &QPushButton::clicked, this, [this, open_target]() {
        auto result = open_target(UiOpenTargets::TargetId::VersionManager);
        auto version_dialog = qobject_cast<QDialog*>(result.opened_window);
        if (version_dialog) {
            connect(version_dialog, &QDialog::finished, this,
                    [this](int) { LoadVersionComboBox(); });
        }
    });

#ifdef ENABLE_UPDATER
    connect(ui->updaterAct, &QAction::triggered, this, [this]() {
        auto checkUpdate = new CheckUpdate(m_gui_settings, true);
        checkUpdate->exec();
    });
#endif

    connect(ui->aboutAct, &QAction::triggered, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::About); });

    connect(ui->configureHotkeys, &QAction::triggered, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::Hotkeys); });

    connect(ui->setIconSizeTinyAct, &QAction::triggered, this, [this]() {
        if (isTableList) {
            m_game_list_frame->icon_size =
                36; // 36 is the minimum icon size to use due to text disappearing.
            ui->sizeSlider->setValue(0); // icone_size - 36
            m_gui_settings->SetValue(gui::gl_icon_size, 36);
            m_gui_settings->SetValue(gui::gl_slider_pos, 0);
        } else {
            m_game_grid_frame->icon_size = 69;
            ui->sizeSlider->setValue(0); // icone_size - 36
            m_gui_settings->SetValue(gui::gg_icon_size, 69);
            m_gui_settings->SetValue(gui::gg_slider_pos, 9);
            m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
        }
    });

    connect(ui->setIconSizeSmallAct, &QAction::triggered, this, [this]() {
        if (isTableList) {
            m_game_list_frame->icon_size = 64;
            ui->sizeSlider->setValue(28);
            m_gui_settings->SetValue(gui::gl_icon_size, 64);
            m_gui_settings->SetValue(gui::gl_slider_pos, 28);
        } else {
            m_game_grid_frame->icon_size = 97;
            ui->sizeSlider->setValue(28);
            m_gui_settings->SetValue(gui::gg_icon_size, 97);
            m_gui_settings->SetValue(gui::gg_slider_pos, 28);
            m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
        }
    });

    connect(ui->setIconSizeMediumAct, &QAction::triggered, this, [this]() {
        if (isTableList) {
            m_game_list_frame->icon_size = 128;
            ui->sizeSlider->setValue(92);
            m_gui_settings->SetValue(gui::gl_icon_size, 128);
            m_gui_settings->SetValue(gui::gl_slider_pos, 92);
        } else {
            m_game_grid_frame->icon_size = 161;
            ui->sizeSlider->setValue(92);
            m_gui_settings->SetValue(gui::gg_icon_size, 161);
            m_gui_settings->SetValue(gui::gg_slider_pos, 92);
            m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
        }
    });

    connect(ui->setIconSizeLargeAct, &QAction::triggered, this, [this]() {
        if (isTableList) {
            m_game_list_frame->icon_size = 256;
            ui->sizeSlider->setValue(220);
            m_gui_settings->SetValue(gui::gl_icon_size, 256);
            m_gui_settings->SetValue(gui::gl_slider_pos, 220);
        } else {
            m_game_grid_frame->icon_size = 256;
            ui->sizeSlider->setValue(220);
            m_gui_settings->SetValue(gui::gg_icon_size, 256);
            m_gui_settings->SetValue(gui::gg_slider_pos, 220);
            m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
        }
    });

    // handle resize like this for now, we deal with it when we add more docks
    connect(this, &MainWindow::WindowResized, this, [&]() {
        this->resizeDocks({m_dock_widget.data()}, {this->width()}, Qt::Orientation::Horizontal);
    });

    // List
    connect(ui->setlistModeListAct, &QAction::triggered, m_dock_widget.data(), [this]() {
        ui->sizeSlider->setEnabled(true);
        BackgroundMusicPlayer::getInstance().stopMusic();
        QList<int> sizes = {ui->splitter->sizes()};
        m_gui_settings->SetValue(gui::mw_dockWidgetSizes, QVariant::fromValue(sizes));
        m_gui_settings->SetValue(gui::gl_mode, 0);
        CreateDockWindows(false);
        ui->mw_searchbar->setText("");
        SetLastIconSizeBullet();
    });
    // Grid
    connect(ui->setlistModeGridAct, &QAction::triggered, m_dock_widget.data(), [this]() {
        ui->sizeSlider->setEnabled(true);
        BackgroundMusicPlayer::getInstance().stopMusic();
        QList<int> sizes = {ui->splitter->sizes()};
        m_gui_settings->SetValue(gui::mw_dockWidgetSizes, QVariant::fromValue(sizes));
        m_gui_settings->SetValue(gui::gl_mode, 1);
        CreateDockWindows(false);
        ui->mw_searchbar->setText("");
        SetLastIconSizeBullet();
    });
    // Elf Viewer
    connect(ui->setlistElfAct, &QAction::triggered, m_dock_widget.data(), [this]() {
        ui->sizeSlider->setEnabled(false);
        BackgroundMusicPlayer::getInstance().stopMusic();
        QList<int> sizes = {ui->splitter->sizes()};
        m_gui_settings->SetValue(gui::mw_dockWidgetSizes, QVariant::fromValue(sizes));
        m_gui_settings->SetValue(gui::gl_mode, 2);
        CreateDockWindows(false);
        SetLastIconSizeBullet();
    });

    // Cheats/Patches Download.
    connect(ui->downloadCheatsPatchesAct, &QAction::triggered, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::CheatsPatchesDownload); });

    // Dump game list.
    connect(ui->dumpGameListAct, &QAction::triggered, this, [&] {
        QString filePath = qApp->applicationDirPath().append("/GameList.txt");
        QFile file(filePath);
        QTextStream out(&file);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qDebug() << "Failed to open file for writing:" << file.errorString();
            return;
        }
        out << QString("%1 %2 %3 %4 %5\n")
                   .arg("          NAME", -50)
                   .arg("    ID", -10)
                   .arg("FW", -4)
                   .arg(" APP VERSION", -11)
                   .arg("                Path");
        for (const GameInfo& game : m_game_info->m_games) {
            QString game_path;
            Common::FS::PathToQString(game_path, game.path);
            out << QString("%1 %2 %3     %4 %5\n")
                       .arg(QString::fromStdString(game.name), -50)
                       .arg(QString::fromStdString(game.serial), -10)
                       .arg(QString::fromStdString(game.fw), -4)
                       .arg(QString::fromStdString(game.version), -11)
                       .arg(game_path);
        }
    });

    // Package install.
    connect(ui->bootGameAct, &QAction::triggered, this, &MainWindow::BootGame);
    connect(ui->gameInstallPathAct, &QAction::triggered, this, &MainWindow::InstallDirectory);

    // elf viewer
    connect(ui->addElfFolderAct, &QAction::triggered, m_elf_viewer.data(),
            &ElfViewer::OpenElfFolder);

    // Trophy Viewer
    connect(ui->trophyViewerAct, &QAction::triggered, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::TrophyViewer); });

    // Manage Skylanders
    connect(ui->skylanderPortalAction, &QAction::triggered, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::SkylanderPortal); });

    // Manage Infinity Figures
    connect(ui->infinityFiguresAction, &QAction::triggered, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::InfinityBase); });

    // Manage Dimensions Toypad
    connect(ui->dimensionsToypadAction, &QAction::triggered, this,
            [open_target]() { open_target(UiOpenTargets::TargetId::DimensionsToypad); });

    // Themes
    connect(ui->setThemeDark, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::Dark, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::Dark));
        if (isIconBlack) {
            SetUiIcons(false);
            isIconBlack = false;
        }
    });
    connect(ui->setThemeLight, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::Light, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::Light));
        if (!isIconBlack) {
            SetUiIcons(true);
            isIconBlack = true;
        }
    });
    connect(ui->setThemeGreen, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::Green, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::Green));
        if (isIconBlack) {
            SetUiIcons(false);
            isIconBlack = false;
        }
    });
    connect(ui->setThemeBlue, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::Blue, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::Blue));
        if (isIconBlack) {
            SetUiIcons(false);
            isIconBlack = false;
        }
    });
    connect(ui->setThemeViolet, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::Violet, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::Violet));
        if (isIconBlack) {
            SetUiIcons(false);
            isIconBlack = false;
        }
    });
    connect(ui->setThemeGruvbox, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::Gruvbox, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::Gruvbox));
        if (isIconBlack) {
            SetUiIcons(false);
            isIconBlack = false;
        }
    });
    connect(ui->setThemeTokyoNight, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::TokyoNight, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::TokyoNight));
        if (isIconBlack) {
            SetUiIcons(false);
            isIconBlack = false;
        }
    });
    connect(ui->setThemeOled, &QAction::triggered, &m_window_themes, [this]() {
        m_window_themes.SetWindowTheme(Theme::Oled, ui->mw_searchbar);
        m_gui_settings->SetValue(gui::gen_theme, static_cast<int>(Theme::Oled));
        if (isIconBlack) {
            SetUiIcons(false);
            isIconBlack = false;
        }
    });

    QObject::connect(m_ipc_client.get(), &IpcClient::LogEntrySent, this, &MainWindow::PrintLog);
}

void MainWindow::PrintLog(QString entry, QColor textColor) {
    ui->logDisplay->setTextColor(textColor);
    ui->logDisplay->append(entry);
    QScrollBar* sb = ui->logDisplay->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::StartGameWithArgs(QStringList args) {
    BackgroundMusicPlayer::getInstance().stopMusic();
    QString gamePath = "";
    int table_mode = m_gui_settings->GetValue(gui::gl_mode).toInt();
    if (table_mode == 0) {
        if (m_game_list_frame->currentItem()) {
            int itemID = m_game_list_frame->currentItem()->row();
            Common::FS::PathToQString(gamePath, m_game_info->m_games[itemID].path / "eboot.bin");
            runningGameSerial = m_game_info->m_games[itemID].serial;
        }
    } else if (table_mode == 1) {
        if (m_game_grid_frame->cellClicked) {
            int itemID = (m_game_grid_frame->crtRow * m_game_grid_frame->columnCnt) +
                         m_game_grid_frame->crtColumn;
            Common::FS::PathToQString(gamePath, m_game_info->m_games[itemID].path / "eboot.bin");
            runningGameSerial = m_game_info->m_games[itemID].serial;
        }
    } else {
        if (m_elf_viewer->currentItem()) {
            int itemID = m_elf_viewer->currentItem()->row();
            gamePath = m_elf_viewer->m_elf_list[itemID];
        }
    }
    if (gamePath != "") {
        AddRecentFiles(gamePath);
        const auto path = Common::FS::PathFromQString(gamePath);
        if (!std::filesystem::exists(path)) {
            QMessageBox::critical(nullptr, tr("Run Game"), QString(tr("Eboot.bin file not found")));
            return;
        }
        StartEmulator(path, args);

        UpdateToolbarButtons();
    }
}

void MainWindow::StartGame() {
    StartGameWithArgs({});
}

bool isTable;
void MainWindow::SearchGameTable(const QString& text) {
    if (isTableList) {
        if (isTable != true) {
            m_game_info->m_games = m_game_info->m_games_backup;
            m_game_list_frame->PopulateGameList();
            isTable = true;
        }
        for (int row = 0; row < m_game_list_frame->rowCount(); row++) {
            QString game_name = QString::fromStdString(m_game_info->m_games[row].name);
            bool match = (game_name.contains(text, Qt::CaseInsensitive)); // Check only in column 1
            m_game_list_frame->setRowHidden(row, !match);
        }
    } else {
        isTable = false;
        m_game_info->m_games = m_game_info->m_games_backup;
        m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);

        QVector<GameInfo> filteredGames;
        for (const auto& gameInfo : m_game_info->m_games) {
            QString game_name = QString::fromStdString(gameInfo.name);
            if (game_name.contains(text, Qt::CaseInsensitive)) {
                filteredGames.push_back(gameInfo);
            }
        }
        std::sort(filteredGames.begin(), filteredGames.end(), m_game_info->CompareStrings);
        m_game_info->m_games = filteredGames;
        m_game_grid_frame->PopulateGameGrid(filteredGames, true);
    }
}

void MainWindow::ShowGameList() {
    if (ui->showGameListAct->isChecked()) {
        RefreshGameTable();
    } else {
        m_game_grid_frame->clearContents();
        m_game_list_frame->clearContents();
    }
};

void MainWindow::RefreshGameTable() {
    // m_game_info->m_games.clear();
    m_game_info->GetGameInfo(this);
    m_game_list_frame->clearContents();
    m_game_list_frame->PopulateGameList();
    m_game_grid_frame->clearContents();
    m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
    statusBar->clearMessage();
    int numGames = m_game_info->m_games.size();
    QString statusMessage = tr("Games: ") + QString::number(numGames);
    statusBar->showMessage(statusMessage);
    m_game_list_frame->ToggleColumnVisibility();
}

void MainWindow::ConfigureGuiFromSettings() {
    if (!restoreGeometry(m_gui_settings->GetValue(gui::mw_geometry).toByteArray())) {
        // By default, set the window to 70% of the screen
        resize(QGuiApplication::primaryScreen()->availableSize() * 0.7);
    }
    ui->showGameListAct->setChecked(true);
    int table_mode = m_gui_settings->GetValue(gui::gl_mode).toInt();
    if (table_mode == 0) {
        ui->setlistModeListAct->setChecked(true);
    } else if (table_mode == 1) {
        ui->setlistModeGridAct->setChecked(true);
    } else if (table_mode == 2) {
        ui->setlistElfAct->setChecked(true);
    }

    bool showLog = m_gui_settings->GetValue(gui::mw_showLog).toBool();
    ui->showLogAct->setChecked(showLog);

    BackgroundMusicPlayer::getInstance().setVolume(
        m_gui_settings->GetValue(gui::gl_backgroundMusicVolume).toInt());
}

void MainWindow::SaveWindowState() {
    m_gui_settings->SetValue(gui::mw_geometry, saveGeometry(), false);
    QList<int> sizes = {ui->splitter->sizes()};
    m_gui_settings->SetValue(gui::mw_dockWidgetSizes, QVariant::fromValue(sizes));
}

void MainWindow::BootGame() {
    QFileDialog dialog;
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(tr("ELF files (*.bin *.elf *.oelf)"));
    if (dialog.exec()) {
        QStringList fileNames = dialog.selectedFiles();
        int nFiles = fileNames.size();

        if (nFiles > 1) {
            QMessageBox::critical(nullptr, tr("Game Boot"),
                                  QString(tr("Only one file can be selected!")));
        } else {
            std::filesystem::path path = Common::FS::PathFromQString(fileNames[0]);
            if (!std::filesystem::exists(path)) {
                QMessageBox::critical(nullptr, tr("Run Game"),
                                      QString(tr("Eboot.bin file not found")));
                return;
            }
            StartEmulator(path);
        }
    }
}

void MainWindow::InstallDirectory() {
    auto context = BuildOpenTargetContext(this);
    auto result = UiOpenTargets::OpenTarget(UiOpenTargets::TargetId::GameInstall, context,
                                            UiOpenTargets::OpenBehaviorForUi());
    if (result.success) {
        RefreshGameTable();
    }
}

void MainWindow::SetLastUsedTheme() {
    Theme lastTheme = static_cast<Theme>(m_gui_settings->GetValue(gui::gen_theme).toInt());
    m_window_themes.SetWindowTheme(lastTheme, ui->mw_searchbar);

    switch (lastTheme) {
    case Theme::Light:
        ui->setThemeLight->setChecked(true);
        isIconBlack = true;
        break;
    case Theme::Dark:
        ui->setThemeDark->setChecked(true);
        isIconBlack = false;
        SetUiIcons(false);
        break;
    case Theme::Green:
        ui->setThemeGreen->setChecked(true);
        isIconBlack = false;
        SetUiIcons(false);
        break;
    case Theme::Blue:
        ui->setThemeBlue->setChecked(true);
        isIconBlack = false;
        SetUiIcons(false);
        break;
    case Theme::Violet:
        ui->setThemeViolet->setChecked(true);
        isIconBlack = false;
        SetUiIcons(false);
        break;
    case Theme::Gruvbox:
        ui->setThemeGruvbox->setChecked(true);
        isIconBlack = false;
        SetUiIcons(false);
        break;
    case Theme::TokyoNight:
        ui->setThemeTokyoNight->setChecked(true);
        isIconBlack = false;
        SetUiIcons(false);
        break;
    case Theme::Oled:
        ui->setThemeOled->setChecked(true);
        isIconBlack = false;
        SetUiIcons(false);
        break;
    }
}

void MainWindow::SetLastIconSizeBullet() {
    // set QAction bullet point if applicable
    int lastSize = m_gui_settings->GetValue(gui::gl_icon_size).toInt();
    int lastSizeGrid = m_gui_settings->GetValue(gui::gg_icon_size).toInt();
    if (isTableList) {
        switch (lastSize) {
        case 36:
            ui->setIconSizeTinyAct->setChecked(true);
            break;
        case 64:
            ui->setIconSizeSmallAct->setChecked(true);
            break;
        case 128:
            ui->setIconSizeMediumAct->setChecked(true);
            break;
        case 256:
            ui->setIconSizeLargeAct->setChecked(true);
            break;
        }
    } else {
        switch (lastSizeGrid) {
        case 69:
            ui->setIconSizeTinyAct->setChecked(true);
            break;
        case 97:
            ui->setIconSizeSmallAct->setChecked(true);
            break;
        case 161:
            ui->setIconSizeMediumAct->setChecked(true);
            break;
        case 256:
            ui->setIconSizeLargeAct->setChecked(true);
            break;
        }
    }
}

QIcon MainWindow::RecolorIcon(const QIcon& icon, bool isWhite) {
    QPixmap pixmap(icon.pixmap(icon.actualSize(QSize(120, 120))));
    QColor clr(isWhite ? Qt::white : Qt::black);
    QBitmap mask = pixmap.createMaskFromColor(clr, Qt::MaskOutColor);
    pixmap.fill(QColor(isWhite ? Qt::black : Qt::white));
    pixmap.setMask(mask);
    return QIcon(pixmap);
}

void MainWindow::SetUiIcons(bool isWhite) {
    ui->bootGameAct->setIcon(RecolorIcon(ui->bootGameAct->icon(), isWhite));
    ui->shadFolderAct->setIcon(RecolorIcon(ui->shadFolderAct->icon(), isWhite));
    ui->exitAct->setIcon(RecolorIcon(ui->exitAct->icon(), isWhite));
#ifdef ENABLE_UPDATER
    ui->updaterAct->setIcon(RecolorIcon(ui->updaterAct->icon(), isWhite));
#endif
    ui->downloadCheatsPatchesAct->setIcon(
        RecolorIcon(ui->downloadCheatsPatchesAct->icon(), isWhite));
    ui->dumpGameListAct->setIcon(RecolorIcon(ui->dumpGameListAct->icon(), isWhite));
    ui->aboutAct->setIcon(RecolorIcon(ui->aboutAct->icon(), isWhite));
    ui->setlistModeListAct->setIcon(RecolorIcon(ui->setlistModeListAct->icon(), isWhite));
    ui->setlistModeGridAct->setIcon(RecolorIcon(ui->setlistModeGridAct->icon(), isWhite));
    ui->gameInstallPathAct->setIcon(RecolorIcon(ui->gameInstallPathAct->icon(), isWhite));
    ui->menuThemes->setIcon(RecolorIcon(ui->menuThemes->icon(), isWhite));
    ui->menuGame_List_Icons->setIcon(RecolorIcon(ui->menuGame_List_Icons->icon(), isWhite));
    ui->menuUtils->setIcon(RecolorIcon(ui->menuUtils->icon(), isWhite));
    ui->playButton->setIcon(RecolorIcon(ui->playButton->icon(), isWhite));
    ui->pauseButton->setIcon(RecolorIcon(ui->pauseButton->icon(), isWhite));
    ui->stopButton->setIcon(RecolorIcon(ui->stopButton->icon(), isWhite));
    ui->refreshButton->setIcon(RecolorIcon(ui->refreshButton->icon(), isWhite));
    ui->restartButton->setIcon(RecolorIcon(ui->restartButton->icon(), isWhite));
    ui->settingsButton->setIcon(RecolorIcon(ui->settingsButton->icon(), isWhite));
    ui->fullscreenButton->setIcon(RecolorIcon(ui->fullscreenButton->icon(), isWhite));
    ui->controllerButton->setIcon(RecolorIcon(ui->controllerButton->icon(), isWhite));
    ui->keyboardButton->setIcon(RecolorIcon(ui->keyboardButton->icon(), isWhite));
    ui->refreshGameListAct->setIcon(RecolorIcon(ui->refreshGameListAct->icon(), isWhite));
    ui->menuGame_List_Mode->setIcon(RecolorIcon(ui->menuGame_List_Mode->icon(), isWhite));
    ui->trophyViewerAct->setIcon(RecolorIcon(ui->trophyViewerAct->icon(), isWhite));
    ui->skylanderPortalAction->setIcon(RecolorIcon(ui->skylanderPortalAction->icon(), isWhite));
    ui->infinityFiguresAction->setIcon(RecolorIcon(ui->infinityFiguresAction->icon(), isWhite));
    ui->dimensionsToypadAction->setIcon(RecolorIcon(ui->dimensionsToypadAction->icon(), isWhite));
    ui->configureAct->setIcon(RecolorIcon(ui->configureAct->icon(), isWhite));
    ui->addElfFolderAct->setIcon(RecolorIcon(ui->addElfFolderAct->icon(), isWhite));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    emit WindowResized(event);
    QMainWindow::resizeEvent(event);
}

void MainWindow::HandleResize(QResizeEvent* event) {
    if (isTableList) {
        m_game_list_frame->RefreshListBackgroundImage();
    } else {
        m_game_grid_frame->windowWidth = this->width();
        m_game_grid_frame->PopulateGameGrid(m_game_info->m_games, false);
        m_game_grid_frame->RefreshGridBackgroundImage();
    }
}

void MainWindow::AddRecentFiles(QString filePath) {
    QList<QString> list = gui_settings::Var2List(m_gui_settings->GetValue(gui::gen_recentFiles));
    if (!list.empty()) {
        if (filePath == list.at(0)) {
            return;
        }
        auto it = std::find(list.begin(), list.end(), filePath);
        if (it != list.end()) {
            list.erase(it);
        }
    }
    list.insert(list.begin(), filePath);
    if (list.size() > 6) {
        list.pop_back();
    }
    m_gui_settings->SetValue(gui::gen_recentFiles, gui_settings::List2Var(list));
    CreateRecentGameActions(); // Refresh the QActions.
}

void MainWindow::CreateRecentGameActions() {
    m_recent_files_group = new QActionGroup(this);
    ui->menuRecent->clear();
    QList<QString> list = gui_settings::Var2List(m_gui_settings->GetValue(gui::gen_recentFiles));

    for (int i = 0; i < list.size(); i++) {
        QAction* recentFileAct = new QAction(this);
        recentFileAct->setText(list.at(i));
        ui->menuRecent->addAction(recentFileAct);
        m_recent_files_group->addAction(recentFileAct);
    }

    connect(m_recent_files_group, &QActionGroup::triggered, this, [this](QAction* action) {
        auto gamePath = Common::FS::PathFromQString(action->text());
        AddRecentFiles(action->text()); // Update the list.
        if (!std::filesystem::exists(gamePath)) {
            QMessageBox::critical(nullptr, tr("Run Game"), QString(tr("Eboot.bin file not found")));
            return;
        }
        StartEmulator(gamePath);
    });
}

void MainWindow::LoadTranslation() {
    auto language = m_gui_settings->GetValue(gui::gen_guiLanguage).toString();

    const QString base_dir = QStringLiteral(":/translations");
    QString base_path = QStringLiteral("%1/%2.qm").arg(base_dir).arg(language);

    if (QFile::exists(base_path)) {
        if (translator != nullptr) {
            qApp->removeTranslator(translator);
        }

        translator = new QTranslator(qApp);
        if (!translator->load(base_path)) {
            QMessageBox::warning(
                nullptr, QStringLiteral("Translation Error"),
                QStringLiteral("Failed to find load translation file for '%1':\n%2")
                    .arg(language)
                    .arg(base_path));
            delete translator;
        } else {
            qApp->installTranslator(translator);
            ui->retranslateUi(this);
        }
    }
}

void MainWindow::OnLanguageChanged(const QString& locale) {
    m_gui_settings->SetValue(gui::gen_guiLanguage, locale);

    LoadTranslation();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) {
            auto tblMode = m_gui_settings->GetValue(gui::gl_mode).toInt();
            if (tblMode != 2 && (tblMode != 1 || m_game_grid_frame->IsValidCellSelected())) {
                StartGame();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::StartEmulator(std::filesystem::path path, QStringList args) {
    if (EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("Run Game"), QString(tr("Game is already running!")));
        return;
    }

    QString selectedVersion = m_gui_settings->GetValue(gui::vm_versionSelected).toString();
    if (selectedVersion.isEmpty()) {
        QMessageBox::warning(this, tr("No Version Selected"),
                             // clang-format off
        tr("No emulator version was selected.\n"
           "The Version Manager menu will then open.\n"
           "Select an emulator version from the right panel."));
        // clang-format on
        auto versionDialog = new VersionDialog(m_gui_settings, this);
        connect(versionDialog, &QDialog::finished, this, [this](int) { LoadVersionComboBox(); });
        versionDialog->exec();
        return;
    }

    QFileInfo fileInfo(selectedVersion);
    if (!fileInfo.exists()) {
        QMessageBox::critical(nullptr, "shadPS4",
                              QString(tr("Could not find the emulator executable")));
        return;
    }

    QStringList final_args{"--game", QString::fromStdWString(path.wstring())};

    final_args.append(args);

    EmulatorState::GetInstance()->SetGameRunning(true);
    last_game_path = path;

    QString workDir = QDir::currentPath();
    m_ipc_client->startEmulator(fileInfo, final_args, workDir);
    m_ipc_client->setActiveController(GamepadSelect::GetSelectedGamepad());
}

void MainWindow::StartEmulatorExecutable(std::filesystem::path emuPath, QString gameArg,
                                         QStringList args, bool disable_ipc) {
    if (EmulatorState::GetInstance()->IsGameRunning()) {
        QMessageBox::critical(nullptr, tr("Run Emulator"),
                              QString(tr("Emulator is already running!")));
        return;
    }

    bool gameFound = false;
    if (std::filesystem::exists(Common::FS::PathFromQString(gameArg))) {
        last_game_path = Common::FS::PathFromQString(gameArg);
        gameFound = true;
    } else {
        // In install folders, find game folder with same name as gameArg
        const auto install_dir_array = Config::getGameInstallDirs();
        std::vector<bool> install_dirs_enabled;

        try {
            install_dirs_enabled = Config::getGameInstallDirsEnabled();
        } catch (...) {
            // If it does not exist, assume that all are enabled.
            install_dirs_enabled.resize(install_dir_array.size(), true);
        }

        for (size_t i = 0; i < install_dir_array.size(); i++) {
            std::filesystem::path dir = install_dir_array[i];
            bool enabled = install_dirs_enabled[i];

            if (enabled && std::filesystem::exists(dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (entry.is_directory()) {
                        if (entry.path().filename().string() == gameArg.toStdString()) {
                            last_game_path = entry.path() / "eboot.bin";
                            gameFound = true;
                            break;
                        }
                    }
                }
            }

            if (gameFound)
                break;
        }
    }
    if (!gameArg.isEmpty()) {
        if (!gameFound) {
            QMessageBox::critical(nullptr, "shadPS4",
                                  QString(tr("Invalid game argument provided")));
            quick_exit(1);
        }

        QStringList game_args{"--game", QString::fromStdWString(last_game_path.wstring())};
        args.append(game_args);
    }

    QFileInfo fileInfo(emuPath);
    if (!fileInfo.exists()) {
        QMessageBox::critical(nullptr, "shadPS4",
                              QString(tr("Could not find the emulator executable")));
        return;
    }

    EmulatorState::GetInstance()->SetGameRunning(true);
    QString workDir = QDir::currentPath();
    m_ipc_client->startEmulator(fileInfo, args, workDir, disable_ipc);
}

void MainWindow::RunGame() {
    auto gameInfo = GameInfoClass();
    auto dir = last_game_path.parent_path();
    auto info = gameInfo.readGameInfo(dir);
    auto appVersion = info.version;
    auto gameSerial = info.serial;
    auto patches = MemoryPatcher::readPatches(gameSerial, appVersion);
    for (auto patch : patches) {
        m_ipc_client->sendMemoryPatches(patch.modName, patch.address, patch.value, patch.target,
                                        patch.size, patch.maskOffset, patch.littleEndian,
                                        patch.mask, patch.maskOffset);
    }

    m_ipc_client->startGame();
}

void MainWindow::RestartEmulator() {
    QString exe = m_gui_settings->GetValue(gui::vm_versionSelected).toString();
    QStringList args{"--game", QString::fromStdWString(last_game_path.wstring())};

    if (m_ipc_client->parsedArgs.size() > 0) {
        args.clear();
        for (auto arg : m_ipc_client->parsedArgs) {
            args.append(QString::fromStdString(arg));
        }
        m_ipc_client->parsedArgs.clear();
    }

    QFileInfo fileInfo(exe);
    QString workDir = fileInfo.absolutePath();

    m_ipc_client->startEmulator(fileInfo, args, workDir);
}

void MainWindow::LoadVersionComboBox() {
    ui->versionComboBox->clear();
    ui->versionComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    QString savedVersionPath = m_gui_settings->GetValue(gui::vm_versionSelected).toString();
    auto versions = VersionManager::GetVersionList();

    std::sort(versions.begin(), versions.end(), [](const auto& a, const auto& b) {
        auto getOrder = [](int type) {
            switch (type) {
            case 1: // Pre-release
                return 0;
            case 0: // Release
                return 1;
            case 2: // Local
                return 2;
            default:
                return 3;
            }
        };

        int orderA = getOrder(static_cast<int>(a.type));
        int orderB = getOrder(static_cast<int>(b.type));

        if (orderA != orderB)
            return orderA < orderB;

        if (a.type == VersionManager::VersionType::Release) {
            static QRegularExpression versionRegex("^v\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$");
            QRegularExpressionMatch matchA = versionRegex.match(QString::fromStdString(a.name));
            QRegularExpressionMatch matchB = versionRegex.match(QString::fromStdString(b.name));

            if (matchA.hasMatch() && matchB.hasMatch()) {
                int majorA = matchA.captured(1).toInt();
                int minorA = matchA.captured(2).toInt();
                int patchA = matchA.captured(3).toInt();
                int majorB = matchB.captured(1).toInt();
                int minorB = matchB.captured(2).toInt();
                int patchB = matchB.captured(3).toInt();

                if (majorA != majorB)
                    return majorA > majorB;
                if (minorA != minorB)
                    return minorA > minorB;
                return patchA > patchB;
            }
        }

        return QString::fromStdString(a.name).compare(QString::fromStdString(b.name),
                                                      Qt::CaseInsensitive) < 0;
    });

    if (versions.empty()) {
        ui->versionComboBox->addItem(tr("None"));
        ui->versionComboBox->setCurrentIndex(0);
        return;
    }

    for (const auto& v : versions) {
        ui->versionComboBox->addItem(QString::fromStdString(v.name),
                                     QString::fromStdString(v.path));
    }

    int selectedIndex = ui->versionComboBox->findData(savedVersionPath);
    if (selectedIndex >= 0) {
        ui->versionComboBox->setCurrentIndex(selectedIndex);
    } else {
        ui->versionComboBox->setCurrentIndex(0);
    }

    connect(ui->versionComboBox, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) {
                QString fullPath = ui->versionComboBox->itemData(index).toString();
                m_gui_settings->SetValue(gui::vm_versionSelected, fullPath);
            });

    ui->versionComboBox->adjustSize();
}
