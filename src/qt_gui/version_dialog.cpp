// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressBar>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <common/path_util.h>

#include "common/config.h"
#include "gui_settings.h"
#include "qt_gui/main_window.h"
#include "ui_version_dialog.h"
#include "version_dialog.h"

VersionDialog::VersionDialog(std::shared_ptr<gui_settings> gui_settings, QWidget* parent)
    : QDialog(parent), ui(new Ui::VersionDialog), m_gui_settings(std::move(gui_settings)) {
    ui->setupUi(this);
    this->setMinimumSize(670, 350);

    ui->checkOnStartupCheckBox->setChecked(
        m_gui_settings->GetValue(gui::vm_checkOnStartup).toBool());
    ui->showChangelogCheckBox->setChecked(m_gui_settings->GetValue(gui::vm_showChangeLog).toBool());

    connect(ui->checkOnStartupCheckBox, &QCheckBox::toggled, this,
            [this](bool checked) { m_gui_settings->SetValue(gui::vm_checkOnStartup, checked); });
    connect(ui->showChangelogCheckBox, &QCheckBox::toggled, this,
            [this](bool checked) { m_gui_settings->SetValue(gui::vm_showChangeLog, checked); });

    connect(this, &VersionDialog::WindowResized, this, &VersionDialog::HandleResize);

    networkManager = new QNetworkAccessManager(this);

    if (m_gui_settings->GetValue(gui::vm_versionPath).toString() == "") {
        QString versionDir = QString::fromStdString(
            Common::FS::GetUserPath(Common::FS::PathType::VersionDir).string());
        QDir dir(versionDir);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        m_gui_settings->SetValue(gui::vm_versionPath, versionDir);
    }

    ui->currentVersionPath->setText(m_gui_settings->GetValue(gui::vm_versionPath).toString());

    LoadInstalledList();

    QStringList cachedVersions = LoadDownloadCache();
    if (!cachedVersions.isEmpty()) {
        PopulateDownloadTree(cachedVersions);
    } else {
        DownloadListVersion();
    }

    connect(ui->browse_versionPath, &QPushButton::clicked, this, [this]() {
        const auto shad_exe_path = m_gui_settings->GetValue(gui::vm_versionPath).toString();
        QString initial_path = shad_exe_path;

        QString shad_folder_path_string = QFileDialog::getExistingDirectory(
            this, tr("Select the folder where the emulator versions will be installed"),
            initial_path);

        auto folder_path = Common::FS::PathFromQString(shad_folder_path_string);
        if (!folder_path.empty()) {
            ui->currentVersionPath->setText(shad_folder_path_string);
            m_gui_settings->SetValue(gui::vm_versionPath, shad_folder_path_string);
            m_gui_settings->SetValue(gui::vm_versionSelected, "");
            LoadInstalledList();
        }
    });

    connect(ui->checkChangesVersionButton, &QPushButton::clicked, this,
            [this]() { LoadInstalledList(); });

    connect(ui->addCustomVersionButton, &QPushButton::clicked, this, [this]() {
        QString exePath;

#ifdef Q_OS_WIN
        exePath = QFileDialog::getOpenFileName(this, tr("Select executable"), QDir::rootPath(),
                                               tr("Executable (*.exe)"));
#elif defined(Q_OS_LINUX)
    exePath = QFileDialog::getOpenFileName(this, tr("Select executable"), QDir::rootPath(),
                                            tr("Executable (*.AppImage)"));
#elif defined(Q_OS_MACOS)
    exePath = QFileDialog::getOpenFileName(this, tr("Select executable"), QDir::rootPath(),
                                            tr("Executable (*.*)"));
#endif

        if (exePath.isEmpty())
            return;

        bool ok;
        QString folderName =
            QInputDialog::getText(this, tr("Version name"),
                                  tr("Enter the name of this version as it appears in the list."),
                                  QLineEdit::Normal, "", &ok);
        if (!ok || folderName.trimmed().isEmpty())
            return;

        folderName = folderName.trimmed();

        QString basePath = m_gui_settings->GetValue(gui::vm_versionPath).toString();
        QString newFolderPath = QDir(basePath).filePath(folderName);

        QDir dir;
        if (dir.exists(newFolderPath)) {
            QMessageBox::warning(this, tr("Error"), tr("A folder with that name already exists."));
            return;
        }

        if (!dir.mkpath(newFolderPath)) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to create folder."));
            return;
        }

        QFileInfo exeInfo(exePath);
        QString targetFilePath = QDir(newFolderPath).filePath(exeInfo.fileName());

        if (!QFile::copy(exePath, targetFilePath)) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to copy executable."));
            return;
        }

        QMessageBox::information(this, tr("Success"), tr("Version added successfully."));
        LoadInstalledList();
    });

    connect(ui->deleteVersionButton, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* selectedItem = ui->installedTreeWidget->currentItem();
        if (!selectedItem) {
            QMessageBox::warning(
                this, tr("Error"),
                tr("No version selected. Please choose one from the list to delete."));
            return;
        }

        // Get the actual path of the invisible column folder
        QString fullPath = selectedItem->text(4);
        if (fullPath.isEmpty()) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to determine the folder path."));
            return;
        }
        QString folderName = QDir(fullPath).dirName();
        auto reply = QMessageBox::question(this, tr("Delete version"),
                                           tr("Do you want to delete the version") +
                                               QString(" \"%1\" ?").arg(folderName),
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QDir dirToRemove(fullPath);
            if (dirToRemove.exists()) {
                if (!dirToRemove.removeRecursively()) {
                    QMessageBox::critical(this, tr("Error"),
                                          tr("Failed to delete folder.") +
                                              QString("\n \"%1\"").arg(folderName));
                    return;
                }
            }
            LoadInstalledList();
        }
    });

    connect(ui->checkVersionDownloadButton, &QPushButton::clicked, this,
            [this]() { DownloadListVersion(); });

    connect(ui->installedTreeWidget, &QTreeWidget::itemChanged, this,
            &VersionDialog::onItemChanged);

    connect(ui->updatePreButton, &QPushButton::clicked, this, [this]() { checkUpdatePre(true); });
};

VersionDialog::~VersionDialog() {
    delete ui;
}

void VersionDialog::resizeEvent(QResizeEvent* event) {
    emit WindowResized(event);
    QDialog::resizeEvent(event);
}

void VersionDialog::HandleResize(QResizeEvent* event) {
    this->ui->versionTab->resize(this->size());
}

void VersionDialog::onItemChanged(QTreeWidgetItem* item, int column) {
    if (column == 0) {
        if (item->checkState(0) == Qt::Checked) {
            for (int row = 0; row < ui->installedTreeWidget->topLevelItemCount(); ++row) {
                QTreeWidgetItem* topItem = ui->installedTreeWidget->topLevelItem(row);
                if (topItem != item) {
                    topItem->setCheckState(0, Qt::Unchecked);
                    topItem->setSelected(false);
                }
            }
            QString fullPath = item->text(4);
            m_gui_settings->SetValue(gui::vm_versionSelected, fullPath);

            item->setSelected(true);
        } else {
            item->setSelected(false);
        }
    }
}

void VersionDialog::DownloadListVersion() {
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QUrl url("https://api.github.com/repos/shadps4-emu/shadPS4/tags");
    QNetworkRequest request(url);
    QNetworkReply* reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(response);
            if (doc.isArray()) {
                QJsonArray tags = doc.array();
                ui->downloadTreeWidget->clear();

                QTreeWidgetItem* preReleaseItem = nullptr;
                QList<QTreeWidgetItem*> otherItems;
                bool foundPreRelease = false;

                //  > v.0.5.0
                auto isVersionGreaterThan_0_5_0 = [](const QString& tagName) -> bool {
                    QRegularExpression versionRegex(R"(v\.?(\d+)\.(\d+)\.(\d+))");
                    QRegularExpressionMatch match = versionRegex.match(tagName);
                    if (match.hasMatch()) {
                        int major = match.captured(1).toInt();
                        int minor = match.captured(2).toInt();
                        int patch = match.captured(3).toInt();

                        if (major > 0)
                            return true;
                        if (major == 0 && minor >= 5)
                            return true;
                        if (major == 0 && minor == 5 && patch > 0)
                            return true;
                    }
                    return false;
                };

                for (const QJsonValue& value : tags) {
                    QJsonObject tagObj = value.toObject();
                    QString tagName = tagObj["name"].toString();

                    if (tagName.startsWith("Pre-release", Qt::CaseInsensitive)) {
                        if (!foundPreRelease) {
                            preReleaseItem = new QTreeWidgetItem();
                            preReleaseItem->setText(0, "Pre-release");
                            foundPreRelease = true;
                        }
                        continue;
                    }
                    if (!isVersionGreaterThan_0_5_0(tagName)) {
                        continue;
                    }

                    QTreeWidgetItem* item = new QTreeWidgetItem();
                    item->setText(0, tagName);
                    otherItems.append(item);
                }

                // If you didn't find Pre-release, add it manually
                if (!foundPreRelease) {
                    preReleaseItem = new QTreeWidgetItem();
                    preReleaseItem->setText(0, "Pre-release");
                }

                // Add Pre-release first
                if (preReleaseItem) {
                    ui->downloadTreeWidget->addTopLevelItem(preReleaseItem);
                }

                // Add the others
                for (QTreeWidgetItem* item : otherItems) {
                    ui->downloadTreeWidget->addTopLevelItem(item);
                }

                // Assemble the current list
                QStringList versionList;
                for (int i = 0; i < ui->downloadTreeWidget->topLevelItemCount(); ++i) {
                    QTreeWidgetItem* item = ui->downloadTreeWidget->topLevelItem(i);
                    if (item)
                        versionList.append(item->text(0));
                }

                QStringList cachedVersions = LoadDownloadCache();
                if (cachedVersions == versionList) {
                    QMessageBox::information(this, tr("Version list update"),
                                             tr("No news, the version list is already updated."));
                } else {
                    SaveDownloadCache(versionList);
                    QMessageBox::information(
                        this, tr("Version list update"),
                        tr("The latest versions have been added to the list for download."));
                }

                // selects a version in downloadTreeWidget calls the download stream
                InstallSelectedVersion();
            }
        } else {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Error accessing GitHub API") +
                                     QString(":\n%1").arg(reply->errorString()));
        }
        reply->deleteLater();
    });
}

void VersionDialog::InstallSelectedVersion() {
    connect(
        ui->downloadTreeWidget, &QTreeWidget::itemClicked, this,
        [this](QTreeWidgetItem* item, int) {
            if (m_gui_settings->GetValue(gui::vm_versionPath).toString() == "") {

                QMessageBox::StandardButton reply;
                reply = QMessageBox::warning(
                    this, tr("Select the folder where the emulator versions will be installed"),
                    // clang-format off
tr("First you need to choose a location to save the versions in\n'Path to save versions'"));
                // clang-format on
                return;
            }
            QString versionName = item->text(0);
            QString apiUrl;
            QString platform;

#ifdef Q_OS_WIN
            platform = "win64-sdl";
#elif defined(Q_OS_LINUX)
            platform = "linux-sdl";
#elif defined(Q_OS_MAC)
            platform = "macos-sdl";
#endif
            if (versionName == "Pre-release") {
                apiUrl = "https://api.github.com/repos/shadps4-emu/shadPS4/releases";
            } else {
                apiUrl = QString("https://api.github.com/repos/shadps4-emu/"
                                 "shadPS4/releases/tags/%1")
                             .arg(versionName);
            }

            { // Menssage yes/no
                QMessageBox::StandardButton reply;
                reply = QMessageBox::question(this, tr("Confirm Download"),
                                              tr("Do you want to download the version") +
                                                  QString(": %1 ?").arg(versionName),
                                              QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::No)
                    return;
            }

            QNetworkAccessManager* manager = new QNetworkAccessManager(this);
            QNetworkRequest request(apiUrl);
            QNetworkReply* reply = manager->get(request);

            connect(reply, &QNetworkReply::finished, this, [this, reply, platform, versionName]() {
                if (reply->error() != QNetworkReply::NoError) {
                    QMessageBox::warning(this, tr("Error"), reply->errorString());
                    reply->deleteLater();
                    return;
                }
                QByteArray response = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(response);

                QJsonArray assets;
                QJsonObject release;

                if (versionName == "Pre-release") {
                    QJsonArray releases = doc.array();
                    for (const QJsonValue& val : releases) {
                        QJsonObject obj = val.toObject();
                        if (obj["prerelease"].toBool()) {
                            release = obj;
                            assets = obj["assets"].toArray();
                            break;
                        }
                    }
                } else {
                    release = doc.object();
                    assets = release["assets"].toArray();
                }

                QString downloadUrl;
                for (const QJsonValue& val : assets) {
                    QJsonObject obj = val.toObject();
                    QString name = obj["name"].toString();
                    if (name.contains(platform)) {
                        downloadUrl = obj["browser_download_url"].toString();
                        break;
                    }
                }
                if (downloadUrl.isEmpty()) {
                    QMessageBox::warning(this, tr("Error"),
                                         tr("No files available for this platform."));
                    reply->deleteLater();
                    return;
                }

                QString userPath = m_gui_settings->GetValue(gui::vm_versionPath).toString();
                QString fileName = "temp_download_update.zip";
                QString destinationPath = userPath + "/" + fileName;

                QNetworkAccessManager* downloadManager = new QNetworkAccessManager(this);
                QNetworkRequest downloadRequest(downloadUrl);
                QNetworkReply* downloadReply = downloadManager->get(downloadRequest);

                QDialog* progressDialog = new QDialog(this);
                progressDialog->setWindowTitle(
                    tr("Downloading %1 , please wait...").arg(versionName));
                progressDialog->setFixedSize(400, 80);
                progressDialog->setWindowFlags(progressDialog->windowFlags() &
                                               ~Qt::WindowCloseButtonHint);
                QVBoxLayout* layout = new QVBoxLayout(progressDialog);
                QProgressBar* progressBar = new QProgressBar(progressDialog);
                progressBar->setRange(0, 100);
                layout->addWidget(progressBar);
                progressDialog->setLayout(layout);
                progressDialog->show();

                connect(downloadReply, &QNetworkReply::downloadProgress, this,
                        [progressBar](qint64 bytesReceived, qint64 bytesTotal) {
                            if (bytesTotal > 0)
                                progressBar->setValue(
                                    static_cast<int>((bytesReceived * 100) / bytesTotal));
                        });

                QFile* file = new QFile(destinationPath);
                if (!file->open(QIODevice::WriteOnly)) {
                    QMessageBox::warning(this, tr("Error"), tr("Could not save file."));
                    file->deleteLater();
                    downloadReply->deleteLater();
                    return;
                }

                connect(downloadReply, &QNetworkReply::readyRead, this,
                        [file, downloadReply]() { file->write(downloadReply->readAll()); });

                connect(
                    downloadReply, &QNetworkReply::finished, this,
                    [this, file, downloadReply, progressDialog, release, userPath, versionName]() {
                        file->flush();
                        file->close();
                        file->deleteLater();
                        downloadReply->deleteLater();

                        QString releaseName = release["name"].toString();

                        // Remove "shadPS4 " from the beginning, if it exists
                        if (releaseName.startsWith("shadps4 ", Qt::CaseInsensitive)) {
                            releaseName = releaseName.mid(8);
                        }

                        // Remove "codename" if it exists
                        releaseName.replace(QRegularExpression("\\b[Cc]odename\\s+"), "");

                        QString folderName;
                        if (versionName == "Pre-release") {
                            folderName = release["tag_name"].toString();
                        } else {
                            QString datePart = release["published_at"].toString().left(10);
                            folderName = QString("%1 - %2").arg(releaseName, datePart);
                        }

                        QString destFolder = QDir(userPath).filePath(folderName);

                        // extract ZIP
                        QString scriptFilePath;
                        QString scriptContent;
                        QStringList args;
                        QString process;

#ifdef Q_OS_WIN
                        scriptFilePath = userPath + "/extract_update.ps1";
                        scriptContent = QString("New-Item -ItemType Directory -Path \"%1\" "
                                                "-Force\n"
                                                "Expand-Archive -Path \"%2\" "
                                                "-DestinationPath \"%1\" -Force\n"
                                                "Remove-Item -Force \"%2\"\n"
                                                "Remove-Item -Force \"%3\"\n"
                                                "cls\n")
                                            .arg(destFolder)
                                            .arg(userPath + "/temp_download_update.zip")
                                            .arg(scriptFilePath);
                        process = "powershell.exe";
                        args << "-ExecutionPolicy" << "Bypass" << "-File" << scriptFilePath;
#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
                    scriptFilePath = userPath + "/extract_update.sh";
                    scriptContent = QString(
                                        "#!/bin/bash\n"
                                        "mkdir -p \"%1\"\n"
                                        "unzip -o \"%2\" -d \"%1\"\n"
                                        "rm \"%2\"\n"
                                        "rm \"%3\"\n"
                                        "clear\n")
                                        .arg(destFolder)
                                        .arg(userPath + "/temp_download_update.zip")
                                        .arg(scriptFilePath);
                    process = "bash";
                    args << scriptFilePath;
#endif

                        QFile scriptFile(scriptFilePath);
                        if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                            QTextStream out(&scriptFile);
#ifdef Q_OS_WIN
                            scriptFile.write("\xEF\xBB\xBF"); // BOM
#endif
                            out << scriptContent;
                            scriptFile.close();
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
                            scriptFile.setPermissions(QFile::ExeUser | QFile::ReadUser |
                                                      QFile::WriteUser);
#endif
                            QProcess::startDetached(process, args);

                            QTimer::singleShot(
                                4000, this, [this, folderName, progressDialog, versionName]() {
                                    progressDialog->close();
                                    progressDialog->deleteLater();

                                    QString userPath =
                                        m_gui_settings->GetValue(gui::vm_versionPath).toString();
                                    QString fullPath = QDir(userPath).filePath(folderName);

                                    m_gui_settings->SetValue(gui::vm_versionSelected, fullPath);

                                    QMessageBox::information(
                                        this, tr("Confirm Download"),
                                        tr("Version %1 has been downloaded and selected.")
                                            .arg(versionName));

                                    LoadInstalledList();
                                });
                        } else {
                            QMessageBox::warning(this, tr("Error"),
                                                 tr("Failed to create zip extraction script") +
                                                     QString(":\n%1").arg(scriptFilePath));
                        }
                    });
                reply->deleteLater();
            });
        });
}

void VersionDialog::LoadInstalledList() {
    QString path = m_gui_settings->GetValue(gui::vm_versionPath).toString();
    QDir dir(path);
    if (!dir.exists() || path.isEmpty())
        return;

    ui->installedTreeWidget->clear();
    ui->installedTreeWidget->setColumnCount(5);
    ui->installedTreeWidget->setColumnHidden(4, true);

    QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    QRegularExpression versionRegex("^v(\\d+)\\.(\\d+)\\.(\\d+)$");

    QVector<QPair<QVector<int>, QString>> versionedDirs;

    QStringList otherDirs;
    QString savedVersionPath = m_gui_settings->GetValue(gui::vm_versionSelected).toString();

    for (const QString& folder : folders) {
        if (folder == "Pre-release") {
            otherDirs.append(folder);
            continue;
        }
        QRegularExpressionMatch match = versionRegex.match(folder.section(" - ", 0, 0));
        if (match.hasMatch()) {
            QVector<int> versionParts = {match.captured(1).toInt(), match.captured(2).toInt(),
                                         match.captured(3).toInt()};
            versionedDirs.append({versionParts, folder});
        } else {
            otherDirs.append(folder);
        }
    }

    std::sort(otherDirs.begin(), otherDirs.end());

    std::sort(versionedDirs.begin(), versionedDirs.end(), [](const auto& a, const auto& b) {
        if (a.first[0] != b.first[0])
            return a.first[0] > b.first[0];
        if (a.first[1] != b.first[1])
            return a.first[1] > b.first[1];
        return a.first[2] > b.first[2];
    });

    // Add (Pre-release, Test Build...)
    for (const QString& folder : otherDirs) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->installedTreeWidget);
        QString fullPath = QDir(path).filePath(folder);
        item->setText(4, fullPath);
        item->setCheckState(0, Qt::Unchecked);

        if (folder.startsWith("Pre-release-shadPS4")) {
            QStringList parts = folder.split('-');
            item->setText(1, "Pre-release");
            QString shortHash;
            if (parts.size() >= 7) {
                shortHash = parts[6].left(7);
            } else {
                shortHash = "";
            }
            item->setText(2, shortHash);
            if (parts.size() >= 6) {
                QString date = QString("%1-%2-%3").arg(parts[3], parts[4], parts[5]);
                item->setText(3, date);
            } else {
                item->setText(3, "");
            }
        } else if (folder.contains(" - ")) {
            QStringList parts = folder.split(" - ");
            item->setText(1, parts.value(0));
            item->setText(2, parts.value(1));
            item->setText(3, parts.value(2));
        } else {
            item->setText(1, folder);
            item->setText(2, "");
            item->setText(3, "");
        }

        if (fullPath == savedVersionPath) {
            item->setCheckState(0, Qt::Checked);
        }
    }

    // Add versions
    for (const auto& pair : versionedDirs) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->installedTreeWidget);
        QString fullPath = QDir(path).filePath(pair.second);
        item->setText(4, fullPath);
        item->setCheckState(0, Qt::Unchecked);

        if (pair.second.contains(" - ")) {
            QStringList parts = pair.second.split(" - ");
            item->setText(1, parts.value(0));
            item->setText(2, parts.value(1));
            item->setText(3, parts.value(2));
        } else {
            item->setText(1, pair.second);
            item->setText(2, "");
            item->setText(3, "");
        }

        if (fullPath == savedVersionPath) {
            item->setCheckState(0, Qt::Checked);
        }
    }
    ui->installedTreeWidget->resizeColumnToContents(0);
    ui->installedTreeWidget->resizeColumnToContents(1);
    ui->installedTreeWidget->resizeColumnToContents(2);
    ui->installedTreeWidget->setColumnWidth(1, ui->installedTreeWidget->columnWidth(1) + 10);
    ui->installedTreeWidget->setColumnWidth(2, ui->installedTreeWidget->columnWidth(2) + 20);
}

QStringList VersionDialog::LoadDownloadCache() {
    QString cachePath =
        QDir(m_gui_settings->GetValue(gui::vm_versionPath).toString()).filePath("cache.version");
    QStringList cachedVersions;
    QFile file(cachePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd())
            cachedVersions.append(in.readLine().trimmed());
    }
    return cachedVersions;
}

void VersionDialog::SaveDownloadCache(const QStringList& versions) {
    QString cachePath =
        QDir(m_gui_settings->GetValue(gui::vm_versionPath).toString()).filePath("cache.version");
    QFile file(cachePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        for (const QString& v : versions)
            out << v << "\n";
    }
}

void VersionDialog::PopulateDownloadTree(const QStringList& versions) {
    ui->downloadTreeWidget->clear();

    QTreeWidgetItem* preReleaseItem = nullptr;
    QList<QTreeWidgetItem*> otherItems;
    bool foundPreRelease = false;

    for (const QString& tagName : versions) {
        if (tagName.startsWith("Pre-release", Qt::CaseInsensitive)) {
            if (!foundPreRelease) {
                preReleaseItem = new QTreeWidgetItem();
                preReleaseItem->setText(0, "Pre-release");
                foundPreRelease = true;
            }
            continue;
        }
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, tagName);
        otherItems.append(item);
    }

    if (!foundPreRelease) {
        preReleaseItem = new QTreeWidgetItem();
        preReleaseItem->setText(0, "Pre-release");
    }

    if (preReleaseItem)
        ui->downloadTreeWidget->addTopLevelItem(preReleaseItem);
    for (QTreeWidgetItem* item : otherItems)
        ui->downloadTreeWidget->addTopLevelItem(item);

    InstallSelectedVersion();
}

void VersionDialog::checkUpdatePre(const bool showMessage) {
    QString versionPath = m_gui_settings->GetValue(gui::vm_versionPath).toString();
    if (versionPath.isEmpty() || !QDir(versionPath).exists()) {
        return;
    }

    QDir dir(versionPath);
    QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    QString localHash;
    QString localFolderName;

    // Browse for local Pre-release folder
    for (const QString& folder : folders) {
        if (folder.startsWith("Pre-release-shadPS4")) {
            QStringList parts = folder.split('-');
            if (parts.size() >= 7) {
                localHash = parts.last();
                localFolderName = folder;
            }
            break;
        }
    }

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.github.com/repos/shadps4-emu/shadPS4/releases"));
    QNetworkReply* reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, localHash, localFolderName, showMessage]() {
                if (reply->error() != QNetworkReply::NoError) {
                    QMessageBox::warning(this, tr("Error"), reply->errorString());
                    reply->deleteLater();
                    return;
                }

                QByteArray resp = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(resp);
                if (!doc.isArray()) {
                    QMessageBox::warning(this, tr("Error"),
                                         tr("The GitHub API response is not a valid JSON array."));
                    reply->deleteLater();
                    return;
                }

                QJsonArray arr = doc.array();
                QString latestHash;
                QString latestTag;

                for (const QJsonValue& val : arr) {
                    QJsonObject obj = val.toObject();
                    if (obj["prerelease"].toBool()) {
                        QString tag = obj["tag_name"].toString();
                        latestTag = tag;
                        int idx = tag.lastIndexOf('-');
                        if (idx != -1 && idx + 1 < tag.length()) {
                            latestHash = tag.mid(idx + 1);
                        }
                        break;
                    }
                }

                if (latestHash.isEmpty()) {
                    QMessageBox::warning(this, tr("Error"),
                                         tr("Unable to get hash of latest pre-release"));
                    reply->deleteLater();
                    return;
                }

                if (localHash.isEmpty()) {
                    QMessageBox::StandardButton reply =
                        QMessageBox::question(this, tr("No pre-release found"),
                                              // clang-format off
            tr("You don't have any pre-release installed yet.\nWould you like to download it now?"),
                                              // clang-format on
                                              QMessageBox::Yes | QMessageBox::No);
                    if (reply == QMessageBox::Yes) {
                        installPreReleaseByTag(latestTag);
                    }
                    return;
                }

                if (latestHash == localHash) {
                    if (showMessage) {
                        QMessageBox::information(
                            this, tr("Auto Updater - Emulator"),
                            tr("You already have the latest pre-release version."));
                    }
                } else {
                    showPreReleaseUpdateDialog(localHash, latestHash, latestTag);
                }
                reply->deleteLater();
            });
}

void VersionDialog::showPreReleaseUpdateDialog(const QString& localHash, const QString& latestHash,
                                               const QString& latestTag) {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Auto Updater - Emulator"));

    QVBoxLayout* mainLayout = new QVBoxLayout(&dialog);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* imageLabel = new QLabel(&dialog);
    QPixmap pixmap(":/images/shadps4.png");
    imageLabel->setPixmap(pixmap);
    imageLabel->setScaledContents(true);
    imageLabel->setFixedSize(50, 50);

    QLabel* titleLabel = new QLabel("<h2>" + tr("Update Available (Emulator)") + "</h2>", &dialog);

    headerLayout->addWidget(imageLabel);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);

    mainLayout->addLayout(headerLayout);

    QString labelText = QString("<table>"
                                "<tr><td><b>%1:</b></td><td>%2</td></tr>"
                                "<tr><td><b>%3:</b></td><td>%4</td></tr>"
                                "</table>")
                            .arg(tr("Current Version"), localHash.left(7), tr("Latest Version"),
                                 latestHash.left(7));
    QLabel* infoLabel = new QLabel(labelText, &dialog);
    mainLayout->addWidget(infoLabel);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QLabel* questionLabel = new QLabel(tr("Do you want to update?"), &dialog);
    QPushButton* btnUpdate = new QPushButton(tr("Update"), &dialog);
    QPushButton* btnCancel = new QPushButton(tr("No"), &dialog);

    btnLayout->addWidget(questionLabel);
    btnLayout->addStretch(1);
    btnLayout->addWidget(btnUpdate);
    btnLayout->addWidget(btnCancel);
    mainLayout->addLayout(btnLayout);

    // Changelog
    QTextBrowser* changelogView = new QTextBrowser(&dialog);
    changelogView->setReadOnly(true);
    changelogView->setVisible(false);
    changelogView->setFixedWidth(500);
    changelogView->setFixedHeight(200);
    mainLayout->addWidget(changelogView);

    QPushButton* toggleButton = new QPushButton(tr("Show Changelog"), &dialog);
    mainLayout->addWidget(toggleButton);

    connect(btnCancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    connect(btnUpdate, &QPushButton::clicked, this, [this, &dialog, latestTag]() {
        installPreReleaseByTag(latestTag);
        dialog.accept();
    });

    connect(toggleButton, &QPushButton::clicked, this,
            [this, changelogView, toggleButton, &dialog, localHash, latestHash, latestTag]() {
                if (!changelogView->isVisible()) {
                    requestChangelog(localHash, latestHash, latestTag, changelogView);
                    changelogView->setVisible(true);
                    toggleButton->setText(tr("Hide Changelog"));
                    dialog.adjustSize();
                } else {
                    changelogView->setVisible(false);
                    toggleButton->setText(tr("Show Changelog"));
                    dialog.adjustSize();
                }
            });
    if (m_gui_settings->GetValue(gui::vm_showChangeLog).toBool()) {
        requestChangelog(localHash, latestHash, latestTag, changelogView);
        changelogView->setVisible(true);
        toggleButton->setText(tr("Hide Changelog"));
        dialog.adjustSize();
    }

    dialog.exec();
}

void VersionDialog::requestChangelog(const QString& localHash, const QString& latestHash,
                                     const QString& latestTag, QTextBrowser* outputView) {
    QString compareUrlString =
        QString("https://api.github.com/repos/shadps4-emu/shadPS4/compare/%1...%2")
            .arg(localHash, latestHash);

    QUrl compareUrl(compareUrlString);
    QNetworkRequest req(compareUrl);
    QNetworkReply* reply = networkManager->get(req);

    connect(
        reply, &QNetworkReply::finished, this, [this, reply, localHash, latestHash, outputView]() {
            if (reply->error() != QNetworkReply::NoError) {
                QMessageBox::warning(this, tr("Error"),
                                     tr("Network error while fetching changelog") + ":\n" +
                                         reply->errorString());
                reply->deleteLater();
                return;
            }
            QByteArray resp = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(resp);
            QJsonObject obj = doc.object();
            QJsonArray commits = obj["commits"].toArray();

            QString changesHtml;
            for (const QJsonValue& cval : commits) {
                QJsonObject cobj = cval.toObject();
                QString msg = cobj["commit"].toObject()["message"].toString();
                int newlinePos = msg.indexOf('\n');
                if (newlinePos != -1) {
                    msg = msg.left(newlinePos);
                }
                if (!changesHtml.isEmpty()) {
                    changesHtml += "<br>";
                }
                changesHtml += "&nbsp;&nbsp;&nbsp;&nbsp;• " + msg;
            }

            // PR number as link ( #123 )
            QRegularExpression re("\\(\\#(\\d+)\\)");
            QString newText;
            int last = 0;
            auto it = re.globalMatch(changesHtml);
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                newText += changesHtml.mid(last, m.capturedStart() - last);
                QString num = m.captured(1);
                newText +=
                    QString("(<a href=\"https://github.com/shadps4-emu/shadPS4/pull/%1\">#%1</a>)")
                        .arg(num);
                last = m.capturedEnd();
            }
            newText += changesHtml.mid(last);
            changesHtml = newText;

            outputView->setOpenExternalLinks(true);
            outputView->setHtml("<h3>" + tr("Changes") + ":</h3>" + changesHtml);
            reply->deleteLater();
        });
}

void VersionDialog::installPreReleaseByTag(const QString& tagName) {
    QString apiUrl =
        QString("https://api.github.com/repos/shadps4-emu/shadPS4/releases/tags/%1").arg(tagName);

    QNetworkAccessManager* mgr = new QNetworkAccessManager(this);
    QNetworkRequest req(apiUrl);
    QNetworkReply* reply = mgr->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, tagName]() {
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Error"), reply->errorString());
            reply->deleteLater();
            return;
        }

        QByteArray bytes = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(bytes);
        QJsonObject obj = doc.object();
        QJsonArray assets = obj["assets"].toArray();

        QString downloadUrl;
        QString platformStr;
#ifdef Q_OS_WIN
        platformStr = "win64-sdl";
#elif defined(Q_OS_LINUX)
        platformStr = "linux-sdl";
#elif defined(Q_OS_MAC)
        platformStr = "macos-sdl";
#endif
        for (const QJsonValue& av : assets) {
            QJsonObject aobj = av.toObject();
            if (aobj["name"].toString().contains(platformStr)) {
                downloadUrl = aobj["browser_download_url"].toString();
                break;
            }
        }
        if (downloadUrl.isEmpty()) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("No download URL found for the specified asset."));
            reply->deleteLater();
            return;
        }
        showDownloadDialog(tagName, downloadUrl);

        reply->deleteLater();
    });
}

void VersionDialog::showDownloadDialog(const QString& tagName, const QString& downloadUrl) {
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Downloading Pre‑release, please wait..."));
    QVBoxLayout* lay = new QVBoxLayout(dlg);

    QProgressBar* progressBar = new QProgressBar(dlg);
    progressBar->setRange(0, 100);
    lay->addWidget(progressBar);

    dlg->setLayout(lay);
    dlg->resize(400, 80);
    dlg->setWindowFlags(dlg->windowFlags() & ~Qt::WindowCloseButtonHint);
    dlg->show();

    QNetworkRequest req(downloadUrl);
    QNetworkAccessManager* mgr = new QNetworkAccessManager(dlg);
    QNetworkReply* reply = mgr->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this, [progressBar](qint64 rec, qint64 tot) {
        if (tot > 0) {
            int perc = static_cast<int>((rec * 100) / tot);
            progressBar->setValue(perc);
        }
    });

    connect(reply, &QNetworkReply::finished, this, [=, this]() {
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Network error while downloading") + ":\n" +
                                     reply->errorString());
            reply->deleteLater();
            dlg->close();
            dlg->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        QString userPath = m_gui_settings->GetValue(gui::vm_versionPath).toString();
        QString zipPath = QDir(userPath).filePath("temp_pre_release_download.zip");

        // Find the old folder that starts with "Pre-release"
        QString oldPreReleaseFolder;
        QDir dir(userPath);
        QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            if (entry.startsWith("Pre-release")) {
                oldPreReleaseFolder = QDir(userPath).filePath(entry);
                break;
            }
        }

        QFile zipFile(zipPath);
        if (!zipFile.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Failed to save download file") + ":\n" + zipPath);
            reply->deleteLater();
            return;
        }
        zipFile.write(data);
        zipFile.close();

        QString destFolder = QDir(userPath).filePath(tagName);
        QString scriptFilePath;
        QString scriptContent;
        QStringList args;
        QString process;

#ifdef Q_OS_WIN
        scriptFilePath = userPath + "/extract_pre_release.ps1";
        scriptContent = QString("Remove-Item -Recurse -Force \"%4\" -ErrorAction SilentlyContinue\n"
                                "New-Item -ItemType Directory -Path \"%1\" -Force\n"
                                "Expand-Archive -Path \"%2\" -DestinationPath \"%1\" -Force\n"
                                "Remove-Item -Force \"%2\"\n"
                                "Remove-Item -Force \"%3\"\n"
                                "cls\n")
                            .arg(destFolder)           // %1 - new destination folder
                            .arg(zipPath)              // %2 - zip path
                            .arg(scriptFilePath)       // %3 - script
                            .arg(oldPreReleaseFolder); // %4 - old folder

        process = "powershell.exe";
        args << "-ExecutionPolicy" << "Bypass" << "-File" << scriptFilePath;
#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        scriptFilePath = userPath + "/extract_pre_release.sh";
        scriptContent = QString("#!/bin/bash\n"
                                "rm -rf \"%4\"\n"
                                "mkdir -p \"%1\"\n"
                                "unzip -o \"%2\" -d \"%1\"\n"
                                "rm \"%2\"\n"
                                "rm \"%3\"\n"
                                "clear\n")
                            .arg(destFolder)
                            .arg(zipPath)
                            .arg(scriptFilePath)
                            .arg(oldPreReleaseFolder);
        process = "bash";
        args << scriptFilePath;
#endif

        QFile scriptFile(scriptFilePath);
        if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&scriptFile);
#ifdef Q_OS_WIN
            scriptFile.write("\xEF\xBB\xBF"); // BOM
#endif
            out << scriptContent;
            scriptFile.close();

#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
            scriptFile.setPermissions(QFile::ExeUser | QFile::ReadUser | QFile::WriteUser);
#endif
            QProcess::startDetached(process, args);

            QTimer::singleShot(4000, this, [=, this]() {
                progressBar->setValue(100);
                dlg->close();
                dlg->deleteLater();

                if (!QDir(destFolder).exists()) {
                    QMessageBox::critical(this, tr("Error"), tr("Extraction failure."));
                    return;
                }

                m_gui_settings->SetValue(gui::vm_versionSelected, destFolder);

                QMessageBox::information(this, tr("Complete installation"),
                                         tr("Pre-release updated successfully") + ":\n" + tagName);

                LoadInstalledList();
            });
        } else {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Failed to create the update script file") + ":\n" +
                                     scriptFilePath);
        }

        reply->deleteLater();
    });
}
