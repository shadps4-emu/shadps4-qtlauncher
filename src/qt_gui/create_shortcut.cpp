// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include "common/versions.h"
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
    for (const auto& v : VersionManager::GetVersionList()) {
        auto* item = new QListWidgetItem(QString::fromStdString(v.name), listWidget);
        item->setData(Qt::UserRole, QString::fromStdString(v.path));
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

    const QString executablePath = listWidget->currentItem()->data(Qt::UserRole).toString();
    emit shortcutRequested(executablePath);
    QWidget::close();
}

ShortcutDialog::~ShortcutDialog() {}
