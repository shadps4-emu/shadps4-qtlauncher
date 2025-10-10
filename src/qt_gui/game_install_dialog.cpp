// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "game_install_dialog.h"
#include "gui_settings.h"

GameInstallDialog::GameInstallDialog() : m_gamesDirectory(nullptr) {
    m_gui_settings = std::make_shared<gui_settings>();

    auto layout = new QVBoxLayout(this);

    layout->addWidget(SetupGamesDirectory());
    layout->addWidget(SetupAddonsDirectory());
    layout->addWidget(SetupVersionDirectory());
    layout->addStretch();
    layout->addWidget(SetupDialogActions());

    setWindowTitle(tr("shadPS4 - Choose directory"));
    setWindowIcon(QIcon(":images/shadps4.ico"));
}

GameInstallDialog::~GameInstallDialog() {}

void GameInstallDialog::BrowseGamesDirectory() {
    auto path = QFileDialog::getExistingDirectory(this, tr("Directory to install games"));

    if (!path.isEmpty()) {
        m_gamesDirectory->setText(QDir::toNativeSeparators(path));
    }
}

void GameInstallDialog::BrowseAddonsDirectory() {
    auto path = QFileDialog::getExistingDirectory(this, tr("Directory to install DLC"));

    if (!path.isEmpty()) {
        m_addonsDirectory->setText(QDir::toNativeSeparators(path));
    }
}

void GameInstallDialog::BrowseVersionDirectory() {
    auto path =
        QFileDialog::getExistingDirectory(this, tr("Directory to install emulator versions"));

    if (!path.isEmpty()) {
        m_versionDirectory->setText(QDir::toNativeSeparators(path));
    }
}

QWidget* GameInstallDialog::SetupGamesDirectory() {
    auto group = new QGroupBox(tr("Directory to install games"));
    auto layout = new QHBoxLayout(group);

    // Input.
    m_gamesDirectory = new QLineEdit();
    QString install_dir;
    std::filesystem::path install_path =
        Config::getGameInstallDirs().empty() ? "" : Config::getGameInstallDirs().front();
    Common::FS::PathToQString(install_dir, install_path);
    m_gamesDirectory->setText(install_dir);
    m_gamesDirectory->setMinimumWidth(400);

    layout->addWidget(m_gamesDirectory);

    // Browse button.
    auto browse = new QPushButton(tr("Browse"));

    connect(browse, &QPushButton::clicked, this, &GameInstallDialog::BrowseGamesDirectory);

    layout->addWidget(browse);

    return group;
}

QWidget* GameInstallDialog::SetupAddonsDirectory() {
    auto group = new QGroupBox(tr("Directory to install DLC"));
    auto layout = new QHBoxLayout(group);

    // Input.
    m_addonsDirectory = new QLineEdit();
    QString install_dir;
    Common::FS::PathToQString(install_dir, Config::getAddonInstallDir());
    m_addonsDirectory->setText(install_dir);
    m_addonsDirectory->setMinimumWidth(400);

    layout->addWidget(m_addonsDirectory);

    // Browse button.
    auto browse = new QPushButton(tr("Browse"));

    connect(browse, &QPushButton::clicked, this, &GameInstallDialog::BrowseAddonsDirectory);

    layout->addWidget(browse);

    return group;
}

QWidget* GameInstallDialog::SetupVersionDirectory() {
    auto group = new QGroupBox(tr("Directory to install emulator versions"));
    auto layout = new QHBoxLayout(group);

    m_versionDirectory = new QLineEdit();
    QString version_dir;
    if (m_gui_settings->GetValue(gui::vm_versionPath).toString().isEmpty()) {
        QString defaultVersionDir = QString::fromStdString(
            Common::FS::GetUserPath(Common::FS::PathType::VersionDir).string());
        m_versionDirectory->setText(defaultVersionDir);
    } else {
        m_versionDirectory->setText(m_gui_settings->GetValue(gui::vm_versionPath).toString());
    }
    m_versionDirectory->setMinimumWidth(400);

    layout->addWidget(m_versionDirectory);

    auto browse = new QPushButton(tr("Browse"));
    connect(browse, &QPushButton::clicked, this, &GameInstallDialog::BrowseVersionDirectory);
    layout->addWidget(browse);

    return group;
}

QWidget* GameInstallDialog::SetupDialogActions() {
    auto actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    connect(actions, &QDialogButtonBox::accepted, this, &GameInstallDialog::Save);
    connect(actions, &QDialogButtonBox::rejected, this, &GameInstallDialog::reject);

    return actions;
}

void GameInstallDialog::Save() {
    // Check games directory.
    auto gamesDirectory = m_gamesDirectory->text();
    auto addonsDirectory = m_addonsDirectory->text();
    auto versionDirectory = m_versionDirectory->text();

    if (gamesDirectory.isEmpty() || !QDir(gamesDirectory).exists() ||
        !QDir::isAbsolutePath(gamesDirectory)) {
        QMessageBox::critical(this, tr("Error"),
                              "The value for location to install games is not valid.");
        return;
    }

    if (addonsDirectory.isEmpty() || !QDir::isAbsolutePath(addonsDirectory)) {
        QMessageBox::critical(this, tr("Error"),
                              "The value for location to install DLC is not valid.");
        return;
    }

    QDir addonsDir(addonsDirectory);
    if (!addonsDir.exists()) {
        if (!addonsDir.mkpath(".")) {
            QMessageBox::critical(this, tr("Error"),
                                  "The DLC install location could not be created.");
            return;
        }
    }

    if (versionDirectory.isEmpty() || !QDir::isAbsolutePath(versionDirectory)) {
        QMessageBox::critical(this, tr("Error"),
                              "The value for location to install emulator versions is not valid.");
        return;
    }

    QDir versionDir(versionDirectory);
    if (!versionDir.exists()) {
        if (!versionDir.mkpath(".")) {
            QMessageBox::critical(this, tr("Error"),
                                  "The emulator version location could not be created.");
            return;
        }
    }

    // Save the directories
    Config::addGameInstallDir(Common::FS::PathFromQString(gamesDirectory));
    Config::setAddonInstallDir(Common::FS::PathFromQString(addonsDirectory));
    m_gui_settings->SetValue(gui::vm_versionPath, versionDirectory);

    const auto config_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    Config::save(config_dir / "config.toml");

    accept();
}
