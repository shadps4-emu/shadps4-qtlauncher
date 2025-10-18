// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDialogButtonBox>
#include <QDirIterator>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include "common/path_util.h"
#include "create_shortcut.h"

ShortcutDialog::ShortcutDialog(std::shared_ptr<gui_settings> settings, QWidget* parent)
    : QDialog(parent), m_gui_settings(std::move(settings)) {
    setupUI();
    resize(600, 50);

    this->setWindowTitle(tr("Select Version"));
}

void ShortcutDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* versionLabel =
        new QLabel(QString("<b>%1</b>").arg(tr("Select version for shortcut creation")));

    listWidget = new QListWidget(this);
    QString versionFolder = m_gui_settings->GetValue(gui::vm_versionPath).toString();
    QDirIterator versions(versionFolder, QDir::Dirs | QDir::NoDotAndDotDot);

    while (versions.hasNext()) {
        versions.next();
        new QListWidgetItem(versions.fileName(), listWidget);
    }

    QDialogButtonBox* buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    mainLayout->addWidget(versionLabel);
    mainLayout->addWidget(listWidget);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ShortcutDialog::createShortcut);
}

void ShortcutDialog::createShortcut() {
    if (listWidget->selectedItems().empty()) {
        QMessageBox::information(this, tr("No Version Selected"), tr("Select a version first"));
        return;
    }

    QString versionFolder = m_gui_settings->GetValue(gui::vm_versionPath).toString();
    QString versionName = "/" + listWidget->currentItem()->text();
    QString exeName;
#ifdef Q_OS_WIN
    exeName = "/shadPS4.exe";
#elif defined(Q_OS_LINUX)
    exeName = "/Shadps4-sdl.AppImage";
#elif defined(Q_OS_MACOS)
    exeName = "/shadps4";
#endif

    emit shortcutRequested(versionFolder + versionName + exeName);
    QWidget::close();
}

ShortcutDialog::~ShortcutDialog() {}
