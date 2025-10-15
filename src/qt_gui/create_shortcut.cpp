// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "common/path_util.h"
#include "create_shortcut.h"

#ifdef Q_OS_WIN
#include <ShlObj.h>
#include <Windows.h>
#include <objbase.h>
#include <shlguid.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

ShortcutDialog::ShortcutDialog(std::shared_ptr<gui_settings> settings,
                               QVector<GameInfo>& games_info, int id, QWidget* parent)
    : QDialog(parent), m_gui_settings(std::move(settings)), m_games(games_info), itemID(id) {
    setupUI();
    resize(500, 50);

    this->setWindowTitle(tr("Create Shortcut"));
}

void ShortcutDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* selectBinaryLabel =
        new QLabel(tr("<b>%1</b>").arg("Select shadPS4 binary for shortcut creation"));

    QHBoxLayout* selectBinaryHLayout = new QHBoxLayout(this);
    binaryLineEdit = new QLineEdit();
    binaryLineEdit->setPlaceholderText(tr("Select shadPS4 binary"));
    QPushButton* binaryButton = new QPushButton(tr("Browse"));

    selectBinaryHLayout->addWidget(binaryLineEdit);
    selectBinaryHLayout->addWidget(binaryButton);

    QLabel* selectPatchLabel =
        new QLabel(tr("<b>%1</b>").arg("Select patch file for shortcut creation (optional)"));

    QHBoxLayout* selectPatchHLayout = new QHBoxLayout(this);
    patchLineEdit = new QLineEdit();
    patchLineEdit->setPlaceholderText(tr("Select patch file"));
    QPushButton* patchButton = new QPushButton(tr("Browse"));

    selectPatchHLayout->addWidget(patchLineEdit);
    selectPatchHLayout->addWidget(patchButton);

    QDialogButtonBox* buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    mainLayout->addWidget(selectBinaryLabel);
    mainLayout->addLayout(selectBinaryHLayout);
    mainLayout->addWidget(selectPatchLabel);
    mainLayout->addLayout(selectPatchHLayout);
    mainLayout->addWidget(buttonBox);

    connect(binaryButton, &QPushButton::clicked, this, &ShortcutDialog::browseBinary);
    connect(patchButton, &QPushButton::clicked, this, &ShortcutDialog::browsePatch);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ShortcutDialog::createShortcut);
}

void ShortcutDialog::createShortcut() {
    bool fileExists = std::filesystem::exists(Common::FS::PathFromQString(binaryLineEdit->text()));
    if (binaryLineEdit->text() == "" || !fileExists) {
        QMessageBox::information(this, tr("Invalid file"), tr("No valid shadPS4 binary selected"));
        return;
    }

    QString binaryFile = binaryLineEdit->text().replace("\\", "/");
    QString patchFile = patchLineEdit->text().replace("\\", "/");

    // Path to shortcut/link
    QString linkPath;

    // Eboot path
    QString targetPath;
    Common::FS::PathToQString(targetPath, m_games[itemID].path);
    QString ebootPath = targetPath + "/eboot.bin";

    // Get the full path to the icon
    QString iconPath;
    Common::FS::PathToQString(iconPath, m_games[itemID].icon_path);
    QFileInfo iconFileInfo(iconPath);
    QString icoPath = iconFileInfo.absolutePath() + "/" + iconFileInfo.baseName() + ".ico";

#ifdef Q_OS_WIN
    linkPath =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" +
        QString::fromStdString(m_games[itemID].name).remove(QRegularExpression("[\\\\/:*?\"<>|]")) +
        ".lnk";
#else
    linkPath =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" +
        QString::fromStdString(m_games[itemID].name).remove(QRegularExpression("[\\\\/:*?\"<>|]")) +
        ".desktop";
#endif

    // Convert the icon to .ico if necessary
    if (iconFileInfo.suffix().toLower() == "png") {
        // Convert icon from PNG to ICO
        if (convertPngToIco(iconPath, icoPath)) {

#ifdef Q_OS_WIN
            if (createShortcutWin(linkPath, ebootPath, icoPath, binaryFile, patchFile)) {
#else
            if (createShortcutLinux(linkPath, m_games[itemID].name, ebootPath, iconPath,
                                    binaryLineEdit->text(), patchLineEdit->text())) {
#endif
                QMessageBox::information(
                    nullptr, tr("Shortcut creation"),
                    QString(tr("Shortcut created successfully!") + "\n%1").arg(linkPath));
            } else {
                QMessageBox::critical(
                    nullptr, tr("Error"),
                    QString(tr("Error creating shortcut!") + "\n%1").arg(linkPath));
            }
        } else {
            QMessageBox::critical(nullptr, tr("Error"), tr("Failed to convert icon."));
        }

        // If the icon is already in ICO format, we just create the shortcut
    } else {
#ifdef Q_OS_WIN
        if (createShortcutWin(linkPath, ebootPath, iconPath, binaryFile, patchFile)) {
#else
        if (createShortcutLinux(linkPath, m_games[itemID].name, ebootPath, iconPath,
                                binaryLineEdit->text(), patchLineEdit->text())) {
#endif
            QMessageBox::information(
                nullptr, tr("Shortcut creation"),
                QString(tr("Shortcut created successfully!") + "\n%1").arg(linkPath));
        } else {
            QMessageBox::critical(nullptr, tr("Error"),
                                  QString(tr("Error creating shortcut!") + "\n%1").arg(linkPath));
        }
    }

    QWidget::close();
}

void ShortcutDialog::browsePatch() {
    QString patchDir;
    Common::FS::PathToQString(patchDir, Common::FS::GetUserPath(Common::FS::PathType::PatchesDir));
    QString patchFile = QFileDialog::getOpenFileName(nullptr, tr("Select patch file for shortcut"),
                                                     patchDir, "Patch files (*.xml)");

    patchLineEdit->setText(patchFile);
}

void ShortcutDialog::browseBinary() {
    QString versionDir = m_gui_settings->GetValue(gui::vm_versionPath).toString();
    QString defaultPath = versionDir.isEmpty() ? QDir::currentPath() : versionDir;

    QString shadBinary =
#ifdef Q_OS_WIN
        QFileDialog::getOpenFileName(nullptr, tr("Select shadPS4 executable for shortcut"),
                                     defaultPath, "Exe files (*.exe)");
#elif defined(Q_OS_APPLE)
        QFileDialog::getOpenFileName(nullptr, tr("Select shadPS4 binary for shortcut"), defaultPath,
                                     "Binary files (shadps4*)");
#elif defined(Q_OS_LINUX)
        QFileDialog::getOpenFileName(nullptr, tr("Select shadPS4 binary for shortcut"), defaultPath,
                                     "AppImages (*.AppImage);; Binary files (shadps4*)");
#endif
    binaryLineEdit->setText(shadBinary);
}

bool ShortcutDialog::convertPngToIco(const QString& pngFilePath, const QString& icoFilePath) {
    // Load the PNG image
    QImage image(pngFilePath);
    if (image.isNull()) {
        return false;
    }

    // Scale the image to the default icon size (256x256 pixels)
    QImage scaledImage =
        image.scaled(QSize(256, 256), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Convert the image to QPixmap
    QPixmap pixmap = QPixmap::fromImage(scaledImage);

    // Save the pixmap as an ICO file
    if (pixmap.save(icoFilePath, "ICO")) {
        return true;
    } else {
        return false;
    }
}

#ifdef Q_OS_WIN
bool ShortcutDialog::createShortcutWin(const QString& linkPath, const QString& targetPath,
                                       const QString& iconPath, const QString& exePath,
                                       const QString& patchPath) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Create the ShellLink object
    Microsoft::WRL::ComPtr<IShellLink> pShellLink;
    HRESULT hres =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pShellLink));
    if (SUCCEEDED(hres)) {
        // Defines the path to the program executable
        pShellLink->SetPath((LPCWSTR)exePath.utf16());

        // Sets the home directory ("Start in")
        pShellLink->SetWorkingDirectory((LPCWSTR)QFileInfo(exePath).absolutePath().utf16());

        // Set arguments, eboot.bin file location

        QString arguments;
        if (patchPath != "") {
            arguments = QString("-g \"%1\" -p \"%2\"").arg(targetPath, patchPath);
        } else {
            arguments = QString("-g \"%1\"").arg(targetPath);
        }
        pShellLink->SetArguments((LPCWSTR)arguments.utf16());

        // Set the icon for the shortcut
        pShellLink->SetIconLocation((LPCWSTR)iconPath.utf16(), 0);

        // Save the shortcut
        Microsoft::WRL::ComPtr<IPersistFile> pPersistFile;
        hres = pShellLink.As(&pPersistFile);
        if (SUCCEEDED(hres)) {
            hres = pPersistFile->Save((LPCWSTR)linkPath.utf16(), TRUE);
        }
    }

    CoUninitialize();

    return SUCCEEDED(hres);
}
#else
bool ShortcutDialog::createShortcutLinux(const QString& linkPath, const std::string& name,
                                         const QString& targetPath, const QString& iconPath,
                                         const QString& shadBinary, const QString& patchPath) {
    QFile shortcutFile(linkPath);
    if (!shortcutFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, "Error",
                              QString("Error creating shortcut!\n %1").arg(linkPath));
        return false;
    }

    QTextStream out(&shortcutFile);
    out << "[Desktop Entry]\n";
    out << "Version=1.0\n";
    out << "Name=" << QString::fromStdString(name) << "\n";
    if (patchPath != "") {
        out << "Exec=" << shadBinary << " -g" << " \"" << targetPath << "\"" << " -p" << " \""
            << patchPath << "\"\n";
    } else {
        out << "Exec=" << shadBinary << " -g" << " \"" << targetPath << "\"\n";
    }
    out << "Icon=" << iconPath << "\n";
    out << "Terminal=false\n";
    out << "Type=Application\n";
    shortcutFile.close();

    return true;
}
#endif

ShortcutDialog::~ShortcutDialog() {}
