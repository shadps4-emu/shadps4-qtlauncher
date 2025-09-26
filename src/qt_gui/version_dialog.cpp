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
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <common/path_util.h>

#include "common/config.h"
#include "gui_settings.h"
#include "ui_version_dialog.h"
#include "version_dialog.h"

VersionDialog::VersionDialog(std::shared_ptr<gui_settings> gui_settings, QWidget* parent)
    : QDialog(parent), ui(new Ui::VersionDialog), m_gui_settings(std::move(gui_settings)) {
    ui->setupUi(this);

    ui->currentShadPath->setText(m_gui_settings->GetValue(gui::gen_shadPath).toString());
    connect(ui->browse_shad_path, &QPushButton::clicked, this, [this]() {
        const auto shad_exe_path = m_gui_settings->GetValue(gui::gen_shadPath).toString();
        QString initial_path = shad_exe_path;

        QString shad_folder_path_string =
            QFileDialog::getExistingDirectory(this, tr("Select the shadPS4 folder"), initial_path);

        auto folder_path = Common::FS::PathFromQString(shad_folder_path_string);
        if (!folder_path.empty()) {
            ui->currentShadPath->setText(shad_folder_path_string);
            m_gui_settings->SetValue(gui::gen_shadPath, shad_folder_path_string);
        }
    });

    LoadinstalledList();

    connect(ui->checkChangesVersionButton, &QPushButton::clicked, this,
            [this]() { LoadinstalledList(); });

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

        QString basePath = m_gui_settings->GetValue(gui::gen_shadPath).toString();
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
        LoadinstalledList();
    });

    connect(ui->deleteVersionButton, &QPushButton::clicked, this, [this]() {
        QTreeWidgetItem* selectedItem = ui->installedTreeWidget->currentItem();
        if (!selectedItem) {
            QMessageBox::warning(this, tr("Notice"),
                                 tr("No version selected from the Installed list."));
            return;
        }

        QString folderName = selectedItem->text(0);
        QString path = m_gui_settings->GetValue(gui::gen_shadPath).toString();
        QString fullPath = QDir(path).filePath(folderName);

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
            LoadinstalledList();
        }
    });

    connect(ui->checkVersionDownloadButton, &QPushButton::clicked, this, [this]() {
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
                    connect(
                        ui->downloadTreeWidget, &QTreeWidget::itemClicked, this,
                        [this](QTreeWidgetItem* item, int) {
                            if (m_gui_settings->GetValue(gui::gen_shadPath).toString() == "") {

                                QMessageBox::StandardButton reply;
                                reply = QMessageBox::warning(
                                    this, tr("Select the shadPS4 folder"),
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
                                apiUrl =
                                    "https://api.github.com/repos/shadps4-emu/shadPS4/releases";
                            } else {
                                apiUrl = QString("https://api.github.com/repos/shadps4-emu/"
                                                 "shadPS4/releases/tags/%1")
                                             .arg(versionName);
                            }

                            { // Menssage yes/no
                                QMessageBox::StandardButton reply;
                                reply = QMessageBox::question(
                                    this, tr("Download"),
                                    tr("Do you want to download the version:") +
                                        QString(" %1 ?").arg(versionName),
                                    QMessageBox::Yes | QMessageBox::No);

                                if (reply == QMessageBox::No)
                                    return;
                            }

                            QNetworkAccessManager* manager = new QNetworkAccessManager(this);
                            QNetworkRequest request(apiUrl);
                            QNetworkReply* reply = manager->get(request);

                            connect(
                                reply, &QNetworkReply::finished, this,
                                [this, reply, versionName, platform]() {
                                    if (reply->error() != QNetworkReply::NoError) {
                                        QMessageBox::warning(this, tr("Error"),
                                                             reply->errorString());
                                        reply->deleteLater();
                                        return;
                                    }

                                    QByteArray response = reply->readAll();
                                    QJsonDocument doc = QJsonDocument::fromJson(response);

                                    QJsonArray assets;

                                    if (versionName == "Pre-release") {
                                        QJsonArray releases = doc.array();
                                        for (const QJsonValue& val : releases) {
                                            QJsonObject obj = val.toObject();
                                            if (obj["prerelease"].toBool()) {
                                                assets = obj["assets"].toArray();
                                                break;
                                            }
                                        }
                                    } else {
                                        QJsonObject release = doc.object();
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
                                        QMessageBox::warning(
                                            this, tr("Error"),
                                            tr("No files available for this platform."));
                                        reply->deleteLater();
                                        return;
                                    }

                                    // Destination directory
                                    QString userPath =
                                        m_gui_settings->GetValue(gui::gen_shadPath).toString();
                                    QString fileName = "temp_download_update.zip";
                                    QString destinationPath = userPath + "/" + fileName;

                                    // Start download
                                    QNetworkAccessManager* downloadManager =
                                        new QNetworkAccessManager(this);
                                    QNetworkRequest downloadRequest(downloadUrl);
                                    QNetworkReply* downloadReply =
                                        downloadManager->get(downloadRequest);

                                    QDialog* progressDialog = new QDialog(this);
                                    progressDialog->setWindowTitle(tr("Downloading") +
                                                                   QString(" %1").arg(versionName));
                                    progressDialog->setFixedSize(400, 80);

                                    QVBoxLayout* layout = new QVBoxLayout(progressDialog);
                                    QProgressBar* progressBar = new QProgressBar(progressDialog);
                                    progressBar->setRange(0, 100);
                                    layout->addWidget(progressBar);
                                    progressDialog->setLayout(layout);
                                    progressDialog->show();

                                    // Updates progress based on bytes downloaded
                                    connect(downloadReply, &QNetworkReply::downloadProgress, this,
                                            [progressBar](qint64 bytesReceived, qint64 bytesTotal) {
                                                if (bytesTotal > 0)
                                                    progressBar->setValue(static_cast<int>(
                                                        (bytesReceived * 100) / bytesTotal));
                                            });

                                    QFile* file = new QFile(destinationPath);
                                    if (!file->open(QIODevice::WriteOnly)) {
                                        QMessageBox::warning(this, tr("Error"),
                                                             tr("Could not save file."));
                                        file->deleteLater();
                                        downloadReply->deleteLater();
                                        return;
                                    }

                                    connect(downloadReply, &QNetworkReply::readyRead, this,
                                            [file, downloadReply]() {
                                                file->write(downloadReply->readAll());
                                            });

                                    connect(
                                        downloadReply, &QNetworkReply::finished, this,
                                        [this, file, downloadReply, progressDialog, versionName]() {
                                            file->flush();
                                            file->close();

                                            file->deleteLater();
                                            downloadReply->deleteLater();

                                            QString shadPath =
                                                m_gui_settings->GetValue(gui::gen_shadPath)
                                                    .toString();
                                            QString zipPath =
                                                shadPath + "/temp_download_update.zip";
                                            QString destFolder = shadPath + "/" + versionName;

                                            // script to extract ZIP
                                            QString scriptFilePath;
                                            QString scriptContent;
                                            QStringList args;
                                            QString process;
#ifdef Q_OS_WIN
                                            scriptFilePath = shadPath + "/extract_update.ps1";
                                            scriptContent =
                                                QString(
                                                    "New-Item -ItemType Directory -Path \"%1\" "
                                                    "-Force\n"
                                                    "Expand-Archive -Path \"%2\" "
                                                    "-DestinationPath \"%1\" -Force\n"
                                                    "Remove-Item -Force \"%2\"\n" // delete ZIP
                                                    "Remove-Item -Force \"%3\"\n" // delte script
                                                    "cls\n")                      // clean console
                                                    .arg(destFolder)
                                                    .arg(zipPath)
                                                    .arg(scriptFilePath);
                                            process = "powershell.exe";
                                            args << "-ExecutionPolicy" << "Bypass" << "-File"
                                                 << scriptFilePath;

#elif defined(Q_OS_LINUX) || defined(Q_OS_MAC)
                                                scriptFilePath = shadPath + "/extract_update.sh";
                                                scriptContent =
                                                    QString("#!/bin/bash\n"
                                                            "mkdir -p \"%1\"\n"
                                                            "unzip -o \"%2\" -d \"%1\"\n"
                                                            "rm \"%2\"\n" // delete ZIP
                                                            "rm \"%3\"\n" // delte script
                                                            "clear\n")    // clean console
                                                        .arg(destFolder)
                                                        .arg(zipPath)
                                                        .arg(scriptFilePath);
                                                process = "bash";
                                                args << scriptFilePath;
#endif
                                            QFile scriptFile(scriptFilePath);
                                            if (scriptFile.open(QIODevice::WriteOnly |
                                                                QIODevice::Text)) {
                                                QTextStream out(&scriptFile);
#ifdef Q_OS_WIN
                                                scriptFile.write("\xEF\xBB\xBF"); // BOM
#endif
                                                out << scriptContent;
                                                scriptFile.close();
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
                                                scriptFile.setPermissions(QFile::ExeUser |
                                                                          QFile::ReadUser |
                                                                          QFile::WriteUser);
#endif

                                                QProcess::startDetached(process, args);

                                                // wait 4 seconds while extracting before message
                                                QTimer::singleShot(
                                                    4000, this,
                                                    [this, versionName, destFolder,
                                                     progressDialog]() {
                                                        progressDialog->close();
                                                        progressDialog->deleteLater();

                                                        QMessageBox::information(
                                                            this, tr("Download"),
                                                            tr("Version %1 has been downloaded")
                                                                .arg(versionName));

                                                        VersionDialog::LoadinstalledList();
                                                    });

                                            } else {
                                                QMessageBox::warning(
                                                    this, tr("Error"),
                                                    tr("Failed to create zip extraction script:") +
                                                        QString("\n%1").arg(scriptFilePath));
                                            }
                                        });

                                    reply->deleteLater();
                                });
                        });
                }
            } else {
                QMessageBox::warning(this, tr("Error"),
                                     tr("Error accessing GitHub API") +
                                         QString(":\n%1").arg(reply->errorString()));
            }
            reply->deleteLater();
        });
    });
}

VersionDialog::~VersionDialog() {
    delete ui;
}

void VersionDialog::LoadinstalledList() {
    ui->installedTreeWidget->clear();

    QString path = m_gui_settings->GetValue(gui::gen_shadPath).toString();
    QDir dir(path);
    if (!dir.exists())
        return;

    QStringList folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QRegularExpression versionRegex("^v\\.(\\d+)\\.(\\d+)\\.(\\d+)$");

    QVector<QPair<QVector<int>, QString>> versionedDirs;
    QStringList otherDirs;

    for (const QString& folder : folders) {
        QRegularExpressionMatch match = versionRegex.match(folder);
        if (match.hasMatch()) {
            QVector<int> versionParts;
            for (int i = 1; i <= match.lastCapturedIndex(); ++i) {
                versionParts.append(match.captured(i).toInt());
            }
            versionedDirs.append({versionParts, folder});
        } else {
            otherDirs.append(folder);
        }
    }

    std::sort(otherDirs.begin(), otherDirs.end());
    std::sort(versionedDirs.begin(), versionedDirs.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    for (const QString& folder : otherDirs) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->installedTreeWidget);
        item->setText(0, folder);
    }

    for (const auto& pair : versionedDirs) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->installedTreeWidget);
        item->setText(0, pair.second);
    }
}
